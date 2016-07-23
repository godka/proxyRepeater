#include "testSrslibrtmp.hh"

static unsigned rtspClientCount = 0;
char eventLoopWatchVariable = 0;
//RTSPServer* rtspServer;

int main(int argc, char** argv) {
	OutPacketBuffer::maxSize = DUMMY_SINK_RECEIVE_BUFFER_SIZE;

	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

	// We need at least one "rtsp://" URL argument:
	if (argc < 2) {
		usage(*env, argv[0]);
		exit(1);
	}

	//rtspServer = RTSPServer::createNew(*env);

	// There are argc-1 URLs: argv[1] through argv[argc-1].  Open and start streaming each one:
	for (int i = 1; i <= argc - 1; ++i) {
		openURL(*env, argv[0], argv[i]);
	}
	env->taskScheduler().doEventLoop(&eventLoopWatchVariable);
	return 0;
}

void openURL(UsageEnvironment& env, char const* progName, char const* rtspURL) {
	RTSPClient* rtspClient = ourRTSPClient::createNew(env, rtspURL, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
	if (rtspClient == NULL) {
		env << "Failed to create a RTSP client for URL \"" << rtspURL << "\": " << env.getResultMsg() << "\n";
		return;
	}

	++rtspClientCount;

	StreamClientState& scs = ((ourRTSPClient*) rtspClient)->scs;
	scs.rtspUrl = strDup(rtspClient->url());

	sprintf(scs.streamName, "stream%d", rtspClientCount);

	char tmp[120];
	//sprintf(tmp, "rtmp://117.135.196.137:11935/srs?nonce=1468312250651&token=f93e7db777ca7cf05d83210b3c64dad5/%s", scs.streamName);
	sprintf(tmp, "rtmp://10.196.230.147:1935/srs/%s", scs.streamName);
	scs.rtmpUrl = strDup(tmp);

	scs.rtmpClient = ourRTMPClient::createNew(env, scs.rtmpUrl);
	if(scs.rtmpClient != NULL) {
		rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
	}
}

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString) {
	do {
		UsageEnvironment& env = rtspClient->envir();
		StreamClientState& scs = ((ourRTSPClient*) rtspClient)->scs;

		if (resultCode != 0) {
			env << *rtspClient << "Failed to get a SDP description: " << resultString << "\n";
			delete[] resultString;
			break;
		}

		if(strstr(resultString, "m=video") == NULL) {
			env << *rtspClient << "Not found video by SDP description: " << resultString << "\n";
			delete[] resultString;
			break;
		}

		char* const sdpDescription = resultString;
#ifdef DEBUG
		env << *rtspClient << "Got a SDP description:\n" << sdpDescription << "\n";
#endif
		// Create a media session object from this SDP description:
		scs.session = MediaSession::createNew(env, sdpDescription);
		delete[] sdpDescription; // because we don't need it anymore

		if (scs.session == NULL) {
			env << *rtspClient << "Failed to create a MediaSession object from the SDP description: " << env.getResultMsg() << "\n";
			break;
		} else if (!scs.session->hasSubsessions()) {
			env << *rtspClient << "This session has no media subsessions (i.e., no \"m=\" lines)\n";
			break;
		}

		scs.iter = new MediaSubsessionIterator(*scs.session);
		setupNextSubsession(rtspClient);
		return;
	} while (0);

	// An unrecoverable error occurred with this stream.
	shutdownStream(rtspClient);
}

void setupNextSubsession(RTSPClient* rtspClient) {
	UsageEnvironment& env = rtspClient->envir(); // alias
	StreamClientState& scs = ((ourRTSPClient*) rtspClient)->scs; // alias

	scs.subsession = scs.iter->next();
	if (scs.subsession != NULL) {
		if (!scs.subsession->initiate()) {
			env << *rtspClient << "Failed to initiate the \"" << *scs.subsession << "\" subsession: " << env.getResultMsg() << "\n";
			setupNextSubsession(rtspClient);
		} else {
#ifdef DEBUG
			env << *rtspClient << "Initiated the \"" << *scs.subsession << "\" subsession (";
			if (scs.subsession->rtcpIsMuxed()) {
				env << "client port " << scs.subsession->clientPortNum();
			} else {
				env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum() + 1;
			}
			env << ")\n";
#endif
			// By default, we request that the server stream its data using RTP/UDP.
			// If, instead, you want to request that the server stream via RTP-over-TCP, change the following to True:
			//#define REQUEST_STREAMING_OVER_TCP      True
			rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False, False);
		}
		return;
	}

	if (scs.session->absStartTime() != NULL) {
		rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY, scs.session->absStartTime(), scs.session->absEndTime());
	} else {
		scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
		rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
	}
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString) {
	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		StreamClientState& scs = ((ourRTSPClient*) rtspClient)->scs; // alias

		if (resultCode != 0) {
			env << *rtspClient << "Failed to set up the \"" << *scs.subsession << "\" subsession: " << resultString << "\n";
			break;
		}
#ifdef DEBUG
		env << *rtspClient << "Set up the \"" << *scs.subsession << "\" subsession (";
		if (scs.subsession->rtcpIsMuxed()) {
			env << "client port " << scs.subsession->clientPortNum();
		} else {
			env << "client ports " << scs.subsession->clientPortNum() << "-" << scs.subsession->clientPortNum() + 1;
		}
		env << ")\n";
#endif
		scs.subsession->sink = DummySink::createNew(env, *scs.subsession, *((ourRTMPClient*)scs.rtmpClient));
		// perhaps use your own custom "MediaSink" subclass instead
		if (scs.subsession->sink == NULL) {
			env << *rtspClient << "Failed to create a data sink for the \"" << *scs.subsession << "\" subsession: " << env.getResultMsg() << "\n";
			break;
		}

		DummySink* dummySink = (DummySink*)scs.subsession->sink;
		if( strcasecmp( scs.subsession->mediumName(), "video" ) == 0  &&
			strcasecmp( scs.subsession->codecName(), "H264" ) == 0) {
			const char* spropStr = scs.subsession->attrVal_str("sprop-parameter-sets");
			if( NULL != spropStr ) {
				unsigned numSPropRecords = 0;
				SPropRecord* r = parseSPropParameterSets(spropStr, numSPropRecords);
				for(unsigned n=0; n<numSPropRecords; ++n) {
					u_int8_t nal_unit_type = r[n].sPropBytes[0] & 0x1F;
					if (nal_unit_type == 7) {
						dummySink->spsSize = r[n].sPropLength+4;
						addStartCode(dummySink->sps, r[n].sPropBytes, dummySink->spsSize);
						/*
						unsigned profile_idc, level_idc, width, height, _fps;
						analyze_h264_seq_parameter_set_data(dummySink->sps+4, r[n].sPropLength, profile_idc, level_idc, width, height, dummySink->fps);
						if (_fps > 0) dummySink->fps = _fps;

#ifdef DEBUG
						env << "\n\n\tprofile_idc: " << profile_idc << "\tlevel_idc: " << level_idc  \
						<< "\twidth: " << width << "\theight: " << height << "\tfps: " << dummySink->fps << "\n";
#endif
*/
					} else if (nal_unit_type == 8) {
						dummySink->ppsSize = r[n].sPropLength+4;
						addStartCode(dummySink->pps, r[n].sPropBytes, dummySink->ppsSize);
					}
				}
				delete[] r;
			}
		}
#ifdef DEBUG
		env << *rtspClient << "Created a data sink for the \"" << *scs.subsession << "\" subsession\n";
#endif
		scs.subsession->miscPtr = rtspClient;
		scs.subsession->sink->startPlaying(*(scs.subsession->readSource()), subsessionAfterPlaying, scs.subsession);

		if (scs.subsession->rtcpInstance() != NULL) {
			scs.subsession->rtcpInstance()->setByeHandler(subsessionByeHandler, scs.subsession);
		}
	} while (0);
	delete[] resultString;

	setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString) {
	Boolean success = False;

	do {
		UsageEnvironment& env = rtspClient->envir(); // alias
		StreamClientState& scs = ((ourRTSPClient*) rtspClient)->scs; // alias

		if (resultCode != 0) {
			env << *rtspClient << "Failed to start playing session: " << resultString << "\n";
			break;
		}

		if (scs.duration > 0) {
			unsigned const delaySlop = 2;
			scs.duration += delaySlop;
			unsigned uSecsToDelay = (unsigned) (scs.duration * 1000000);
			scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(uSecsToDelay, (TaskFunc*) streamTimerHandler, scs.subsession);
		}
#ifdef DEBUG
		env << *rtspClient << "Started playing session";
		if (scs.duration > 0) {
			env << " (for up to " << scs.duration << " seconds)";
		}
		env << "...\n";
#endif

		//scs.checkAliveTimerTask = env.taskScheduler().scheduleDelayedTask(CHECK_ALIVE_TASK_TIMER_INTERVAL, (TaskFunc*) sendLivenessCommandHandler, rtspClient);
		announceStream(rtspClient);
		success = True;
	} while (0);
	delete[] resultString;

	if (!success) {
		shutdownStream(rtspClient);
	}
}

void shutdownStream(RTSPClient* rtspClient, int exitCode) {
	UsageEnvironment& env = rtspClient->envir();
	StreamClientState& scs = ((ourRTSPClient*) rtspClient)->scs;
	env << "shutdownStream1" << "\r\n";

	if (scs.session != NULL) {
		Boolean someSubsessionsWereActive = False;
		MediaSubsessionIterator iter(*scs.session);
		MediaSubsession* subsession;

		while ((subsession = iter.next()) != NULL) {
			if (subsession->sink != NULL) {
				Medium::close(subsession->sink);
				subsession->sink = NULL;

				if (subsession->rtcpInstance() != NULL) {
					subsession->rtcpInstance()->setByeHandler(NULL, NULL); // in case the server sends a RTCP "BYE" while handling "TEARDOWN"
				}

				someSubsessionsWereActive = True;
			}
		}

		if (someSubsessionsWereActive) {
			rtspClient->sendTeardownCommand(*scs.session, NULL);
		}
	}

	env << *rtspClient << "Closing the stream.\n";
	Medium::close(rtspClient);
	env << "shutdownStream2" << "\r\n";

	if (--rtspClientCount == 0) {
		exit(exitCode);
	}
}

void subsessionAfterPlaying(void* clientData) {
	MediaSubsession* subsession = (MediaSubsession*) clientData;
	RTSPClient* rtspClient = (RTSPClient*) (subsession->miscPtr);

	Medium::close(subsession->sink);
	subsession->sink = NULL;

	MediaSession& session = subsession->parentSession();
	MediaSubsessionIterator iter(session);
	while ((subsession = iter.next()) != NULL) {
		if (subsession->sink != NULL)
			return;
	}

	shutdownStream(rtspClient);
}

void subsessionByeHandler(void* clientData) {
	MediaSubsession* subsession = (MediaSubsession*) clientData;
	RTSPClient* rtspClient = (RTSPClient*) subsession->miscPtr;
	UsageEnvironment& env = rtspClient->envir(); // alias

	env << *rtspClient << "Received RTCP \"BYE\" on \"" << *subsession << "\" subsession\n";
	subsessionAfterPlaying(subsession);
}

void sendLivenessCommandHandler(void* clientData) {
	ourRTSPClient* rtspClient = (ourRTSPClient*) clientData;
	StreamClientState& scs = rtspClient->scs;
	UsageEnvironment& env = rtspClient->envir(); // alias

	rtspClient->sendGetParameterCommand(*scs.session, NULL, NULL);
	scs.checkAliveTimerTask = env.taskScheduler().scheduleDelayedTask(CHECK_ALIVE_TASK_TIMER_INTERVAL, (TaskFunc*) sendLivenessCommandHandler, rtspClient);
}

void streamTimerHandler(void* clientData) {
  ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
  StreamClientState& scs = rtspClient->scs; 

  scs.streamTimerTask = NULL;
  shutdownStream(rtspClient);
}

void usage(UsageEnvironment& env, char const* progName) {
	env << "Usage: " << progName << " <rtsp-url-1> ... <rtsp-url-N>\n";
	env << "\t(where each <rtsp-url-i> is a \"rtsp://\" URL)\n";
}

void announceStream(RTSPClient* rtspClient) {
	UsageEnvironment& env = rtspClient->envir();
	StreamClientState& scs = ((ourRTSPClient*) rtspClient)->scs;
	env << "RTSP stream, proxying the stream \"" << scs.rtspUrl << "\"\n";
	/*
	if(scs.playbackUrl != NULL) {
		env << "\tPlayback this stream using the URL: " << scs.playbackUrl << "\n";
	}
	*/
	env << "\tPublish endpoint of the stream for URL: " << scs.rtmpUrl<< "\n";
}

void addStartCode(u_int8_t*& dest, u_int8_t* src, unsigned size) {
	if(src != NULL) {
		delete[] dest;
		dest = new u_int8_t[size];
	}

	dest[0] = 0;	dest[1] = 0;
	dest[2] = 0;	dest[3] = 1;

	if(src != NULL) {
		memmove(dest+4, src, size-4);
	}
}

// Implementation of "ourRTSPClient":
ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL,
		int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum) {
	return new ourRTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env, char const* rtspURL, int verbosityLevel,
		char const* applicationName, portNumBits tunnelOverHTTPPortNum) :
		RTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1) {
}

ourRTSPClient::~ourRTSPClient() {
}

// Implementation of "StreamClientState":
StreamClientState::StreamClientState()
  : iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), checkAliveTimerTask(NULL), duration(0.0) {
}

StreamClientState::~StreamClientState() {
	delete iter;
	if (session != NULL) {
		UsageEnvironment& env = session->envir(); // alias
		//env.taskScheduler().unscheduleDelayedTask(checkAliveTimerTask);
		env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
		Medium::close(session);
	}
}

// Implementation of "DummySink":
DummySink* DummySink::createNew(UsageEnvironment& env, MediaSubsession& subsession, ourRTMPClient& client) {
	return  new DummySink(env, subsession, client);
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession, ourRTMPClient& client)
	: MediaSink(env), fHaveWrittenFirstFrame(True), fSubsession(subsession), rtmpClient(client)  {
	sps = new u_int8_t[1];
 	pps = new u_int8_t[1];
 	fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
}

DummySink::~DummySink() {
	delete[] sps;
	delete[] pps;
	delete[] fReceiveBuffer;
}

void DummySink::afterGettingFrame(void* clientData, unsigned frameSize, unsigned numTruncatedBytes,
		struct timeval presentationTime, unsigned durationInMicroseconds) {
	DummySink* sink = (DummySink*)clientData;
	sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

// If you don't want to see debugging output for each received frame, then comment out the following line:
void DummySink::afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
		struct timeval presentationTime, unsigned /*durationInMicroseconds*/) {
	u_int8_t nal_unit_type = fReceiveBuffer[4] & 0x1F; //0xFF;
	unsigned timestamp = presentationTime.tv_sec*1000 + presentationTime.tv_usec/1000;
	do {
		if(fHaveWrittenFirstFrame) {
			if(!isSPS(nal_unit_type) && !isPPS(nal_unit_type) && !isIDR(nal_unit_type))
				break;
			else if(isSPS(nal_unit_type)) {
				spsSize = frameSize+4;
				addStartCode(sps, fReceiveBuffer+4, spsSize);
				//unsigned profile_idc, level_idc, width, height, _fps;
				//analyze_h264_seq_parameter_set_data(sps+4, frameSize, profile_idc, level_idc, width, height, _fps);
				//if(_fps > 0) fps = _fps;
			} else if(isPPS(nal_unit_type)) {
				ppsSize = frameSize+4;
				addStartCode(pps, fReceiveBuffer+4, ppsSize);
			} else if(isIDR(nal_unit_type)) {
				if(!rtmpClient.isConnected) {
					rtmpClient = *(ourRTMPClient::createNew(envir(), rtmpClient.Url));
					do {
						if(rtmpClient.isConnected)
							break;
						usleep(1000*1000);
					} while(1);
				}
				rtmpClient.sendH264FramePacket(envir(), sps, spsSize, timestamp);
				rtmpClient.sendH264FramePacket(envir(), pps, ppsSize, timestamp);
				fHaveWrittenFirstFrame = False;
			}
		}

		if (strcasecmp(fSubsession.mediumName(), "video" ) == 0 &&
				(isIDR(nal_unit_type) || isNonIDR(nal_unit_type))) {
			if(!rtmpClient.isConnected) {
				fHaveWrittenFirstFrame = True;
				break;
			}
			unsigned size = frameSize+4;
			addStartCode(fReceiveBuffer, NULL, size);
			rtmpClient.sendH264FramePacket(envir(), fReceiveBuffer, size, timestamp);
		}

	} while(0);
 	// Then continue, to request the next frame of data:
 	continuePlaying();
}

Boolean DummySink::continuePlaying() {
  	if (fSource == NULL) return False; // sanity check (should not happen)

	// Request the next frame of data from our input source.  "afterGettingFrame()" will get called later, when it arrives:
	fSource->getNextFrame(fReceiveBuffer+4,  DUMMY_SINK_RECEIVE_BUFFER_SIZE-4, afterGettingFrame,
			this, onSourceClosure, this);
	return True;
}

//Implementation of "ourRTMPClient":
ourRTMPClient* ourRTMPClient::createNew(UsageEnvironment& env, char const* rtmpUrl) {
	return new ourRTMPClient(env, rtmpUrl);
}

ourRTMPClient::ourRTMPClient(UsageEnvironment& env, char const* rtmpUrl)
	: rtmp(NULL), hTimestamp(0), dts(0), pts(0) {

	isConnected = False;
	rtmp = srs_rtmp_create(rtmpUrl);
	do {
		if (srs_rtmp_handshake(rtmp) != 0) {
			env << "simple handshake failed." << "\r\n";
        	break;
    	}
 #ifdef DEBUG
		env << "simple handshake success" << "\r\n";
 #endif

		if (srs_rtmp_connect_app(rtmp) != 0) {
			env << "connect vhost/app failed." << "\r\n";
			break;
		}
#ifdef DEBUG
		env << "connect vhost/app success" << "\r\n";
#endif

		int ret = srs_rtmp_publish_stream(rtmp);
		if (ret != 0) {
			env << "publish stream failed.(ret=" << ret << ")\r\n";
			break;
		}
		isConnected = True;
#ifdef DEBUG
		env << "publish stream success" << "\r\n";
#endif
		return;
	} while(0);

	srs_rtmp_destroy(rtmp);
}

ourRTMPClient::~ourRTMPClient() {
	isConnected = False;
	srs_rtmp_destroy(rtmp);
}

void ourRTMPClient::sendH264FramePacket(UsageEnvironment& env, u_int8_t* data, unsigned size, unsigned timestamp) {
	do {
		if(hTimestamp == 0)
			hTimestamp = timestamp;

		pts = dts += (timestamp-hTimestamp);
		hTimestamp = timestamp;

		int ret = srs_h264_write_raw_frames(rtmp, (char*)data, size, dts, pts);
		if (ret != 0) {
			if (srs_h264_is_dvbsp_error(ret)) {
				env << "ignore drop video error, code=" << ret << "\r\n";
			} else if (srs_h264_is_duplicated_sps_error(ret)) {
				env << "ignore duplicated sps, code=" << ret << "\r\n";
			} else if (srs_h264_is_duplicated_pps_error(ret)) {
				env << "ignore duplicated pps, code=" << ret << "\r\n";
			} else {
				env << "send h264 raw data failed. code=" << ret << "\r\n";
				break;
			}
		}
#ifdef DEBUG
		u_int8_t nut = data[4] & 0x1F;
		env << "sent packet: type=video";
		env << ", time=" << dts;
		env << ", size=" << size;
		env << ", b[4]=" << (unsigned char*)data[4] << "(";
		env << (nut == 7? "SPS":(nut == 8? "PPS":(nut == 5? "I":(nut == 1? "P":"Unknown")))) << ")\r\n";
#endif
		return;
	} while(0);

	srs_rtmp_destroy(rtmp);
	isConnected = False;
}

/*
void analyze_h264_seq_parameter_set_data(u_int8_t* sps, unsigned spsSize, unsigned& profile_idc, unsigned& level_idc, unsigned& width, unsigned& height, unsigned& fps){
	unsigned num_units_in_tick = 0, time_scale = 0;

	unsigned forbidden_zero_bit,
		nal_ref_idc,
		nal_unit_type,
		//profile_idc,
		constraint_setN_flag,
		//level_idc,
		seq_parameter_set_id,
		chroma_format_idc,
		delta_scale,
		log2_max_frame_num_minus4,
		pic_order_cnt_type,
		log2_max_pic_order_cnt_lsb_minus4,
		num_ref_frames_in_pic_order_cnt_cycle,
		max_num_ref_frames,
		pic_width_in_mbs_minus1,
		pic_height_in_map_units_minus1,
		frame_crop_left_offset,
		frame_crop_right_offset,
		frame_crop_top_offset,
		frame_crop_bottom_offset,
		aspect_ratio_idc;

	Boolean separate_colour_plane_flag,
		seq_scaling_matrix_present_flag,
		seq_scaling_list_present_flag,
		gaps_in_frame_num_value_allowed_flag,
		frame_mbs_only_flag,
		frame_cropping_flag,
		vui_parameters_present_flag,
		aspect_ratio_info_present_flag,
		overscan_info_present_flag,
		video_signal_type_present_flag,
		colour_description_present_flag,
		chroma_loc_info_present_flag,
		timing_info_present_flag,
		fixed_frame_rate_flag;

	BitVector bv(sps, 0, 8*spsSize);

	forbidden_zero_bit = bv.getBits(1);
	nal_ref_idc = bv.getBits(2);
	nal_unit_type = bv.getBits(5);
	//bv.skipBits(8); // forbidden_zero_bit; nal_ref_idc; nal_unit_type
	profile_idc = bv.getBits(8);
	constraint_setN_flag = bv.getBits(8); // also "reserved_zero_2bits" at end
	level_idc = bv.getBits(8);
	seq_parameter_set_id = bv.get_expGolomb();
	if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122
			|| profile_idc == 244 || profile_idc == 44 || profile_idc == 83
			|| profile_idc == 86 || profile_idc == 118 || profile_idc == 128) {
		chroma_format_idc = bv.get_expGolomb();
		if (chroma_format_idc == 3) {
			separate_colour_plane_flag = bv.get1BitBoolean();
		}
		(void) bv.get_expGolomb(); // bit_depth_luma_minus8
		(void) bv.get_expGolomb(); // bit_depth_chroma_minus8
		bv.skipBits(1); // qpprime_y_zero_transform_bypass_flag
		seq_scaling_matrix_present_flag = bv.get1BitBoolean();
		if (seq_scaling_matrix_present_flag) {
			for (int i = 0; i < ((chroma_format_idc != 3) ? 8 : 12); ++i) {
				seq_scaling_list_present_flag = bv.get1BitBoolean();
				if (seq_scaling_list_present_flag) {
					unsigned sizeOfScalingList = i < 6 ? 16 : 64;
					unsigned lastScale = 8;
					unsigned nextScale = 8;
					for (unsigned j = 0; j < sizeOfScalingList; ++j) {
						if (nextScale != 0) {
							delta_scale = bv.get_expGolomb();
							nextScale = (lastScale + delta_scale + 256) % 256;
						}
						lastScale = (nextScale == 0) ? lastScale : nextScale;
					}
				}
			}
		}
	}

    log2_max_frame_num_minus4 = bv.get_expGolomb();
    pic_order_cnt_type = bv.get_expGolomb();
	if (pic_order_cnt_type == 0) {
		log2_max_pic_order_cnt_lsb_minus4 = bv.get_expGolomb();
	} else if (pic_order_cnt_type == 1) {
		bv.skipBits(1); // delta_pic_order_always_zero_flag
		(void) bv.get_expGolomb(); // offset_for_non_ref_pic
		(void) bv.get_expGolomb(); // offset_for_top_to_bottom_field
		num_ref_frames_in_pic_order_cnt_cycle = bv.get_expGolomb();
		for (unsigned i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i) {
			(void) bv.get_expGolomb(); // offset_for_ref_frame[i]
		}
	}

	max_num_ref_frames = bv.get_expGolomb();
	gaps_in_frame_num_value_allowed_flag = bv.get1BitBoolean();
	pic_width_in_mbs_minus1 = bv.get_expGolomb();
	pic_height_in_map_units_minus1 = bv.get_expGolomb();
	frame_mbs_only_flag = bv.get1BitBoolean();
	if (!frame_mbs_only_flag) {
		bv.skipBits(1); // mb_adaptive_frame_field_flag
	}
	bv.skipBits(1); // direct_8x8_inference_flag
	frame_cropping_flag = bv.get1BitBoolean();
	if (frame_cropping_flag) {
		frame_crop_left_offset = bv.get_expGolomb();
		frame_crop_right_offset = bv.get_expGolomb();
		frame_crop_top_offset = bv.get_expGolomb();
		frame_crop_bottom_offset = bv.get_expGolomb();
	}

	width = (pic_width_in_mbs_minus1 + 1) * 16;
	height = (2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16;
	if (frame_cropping_flag) {
		unsigned int crop_unit_x;
		unsigned int crop_unit_y;
		if (0 == chroma_format_idc) { // monochrome
			crop_unit_x = 1;
			crop_unit_y = 2 - frame_mbs_only_flag;
		} else if (1 == chroma_format_idc) { // 4:2:0
			crop_unit_x = 2;
			crop_unit_y = 2 * (2 - frame_mbs_only_flag);
		} else if (2 == chroma_format_idc) { // 4:2:2
			crop_unit_x = 2;
			crop_unit_y = 2 - frame_mbs_only_flag;
		} else { // 3 == chroma_format_idc   // 4:4:4
			crop_unit_x = 1;
			crop_unit_y = 2 - frame_mbs_only_flag;
		}
		width -= crop_unit_x * (frame_crop_left_offset + frame_crop_right_offset);
		height -= crop_unit_y * (frame_crop_top_offset + frame_crop_bottom_offset);
	}

	vui_parameters_present_flag = bv.get1BitBoolean();
	if (vui_parameters_present_flag) {
		aspect_ratio_info_present_flag = bv.get1BitBoolean();
		if (aspect_ratio_info_present_flag) {
			aspect_ratio_idc = bv.getBits(8);
			if (aspect_ratio_idc == 255) {
				bv.skipBits(32); // sar_width; sar_height
			}
		}
		overscan_info_present_flag = bv.get1BitBoolean();
		if (overscan_info_present_flag) {
			bv.skipBits(1); // overscan_appropriate_flag
		}
		video_signal_type_present_flag = bv.get1BitBoolean();
		if (video_signal_type_present_flag) {
			bv.skipBits(4); // video_format; video_full_range_flag
			colour_description_present_flag = bv.get1BitBoolean();
			if (colour_description_present_flag) {
				bv.skipBits(24); // colour_primaries; transfer_characteristics; matrix_coefficients
			}
		}
		chroma_loc_info_present_flag = bv.get1BitBoolean();
		if (chroma_loc_info_present_flag) {
			(void) bv.get_expGolomb(); // chroma_sample_loc_type_top_field
			(void) bv.get_expGolomb(); // chroma_sample_loc_type_bottom_field
		}

		timing_info_present_flag = bv.get1BitBoolean();
		if (timing_info_present_flag) {
			num_units_in_tick = bv.getBits(32);
			time_scale = bv.getBits(32);
			fixed_frame_rate_flag = bv.get1BitBoolean();
			fps = time_scale / (2 * num_units_in_tick);
		}
	}
} */
