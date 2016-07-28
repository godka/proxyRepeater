#include "testSrslibrtmp.hh"

static unsigned rtspClientCount = 0;
char eventLoopWatchVariable = 0;
char const* progName = NULL;

int main(int argc, char** argv) {
	OutPacketBuffer::maxSize = DUMMY_SINK_RECEIVE_BUFFER_SIZE;
	progName = strDup(argv[0]);

	TaskScheduler* scheduler = BasicTaskScheduler::createNew();
	UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);
	
	// We need at least one "rtsp://" URL argument:
	if (argc < 2) {
		usage(*env);
		exit(1);
	}

	// There are argc-1 URLs: argv[1] through argv[argc-1].  Open and start streaming each one:
	for (int i = 1; i <= argc - 1; ++i) {
		char rtmpUrl[120] = {'\0'};
		//sprintf(rtmpUrl, "rtmp://117.135.196.137:11935/srs?nonce=1468312250651&token=f93e7db777ca7cf05d83210b3c64dad5/stream%d", i);
		sprintf(rtmpUrl, "rtmp://127.0.0.1:1935/srs/stream%d", i);
		openURL(*env, argv[i], rtmpUrl);
	}

	env->taskScheduler().doEventLoop(&eventLoopWatchVariable);
	return 0;
}

void openURL(UsageEnvironment& env, char const* rtspURL, char const* rtmpURL) {
	ourRTSPClient* rtspClient = ourRTSPClient::createNew(env, rtspURL, rtmpURL, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
	if (rtspClient == NULL) {
		env << "Failed to create a RTSP client for URL \"" << rtspURL << "\": " << env.getResultMsg() << "\n";
		return;
	}

	++rtspClientCount;
	rtspClient->sendDescribeCommand(continueAfterDESCRIBE);	
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
		if(ourRTMPClient::createNew(env, rtspClient) == NULL) {
			env << *rtspClient << "Publish the stream. endpoint:\"" << ((ourRTSPClient*) rtspClient)->endpoint() << "\" failed\n";
			break;
		}

		scs.iter = new MediaSubsessionIterator(*scs.session);
		setupNextSubsession(rtspClient);
		return;
	} while (0);

	// An unrecoverable error occurred with this stream.
	shutdownStream(rtspClient);
	usleep(5 * 1000 * 1000);
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
		scs.subsession->miscPtr = rtspClient;

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
		scs.subsession->sink = DummySink::createNew(env, *scs.subsession);
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
					if(isSPS(nal_unit_type)){
						dummySink->sendSpsPacket(r[n].sPropBytes, r[n].sPropLength);
					} else if(isPPS(nal_unit_type)) {
						dummySink->sendPpsPacket(r[n].sPropBytes, r[n].sPropLength);
					}
				}
				delete[] r;
			}
		}
#ifdef DEBUG
		env << *rtspClient << "Created a data sink for the \"" << *scs.subsession << "\" subsession\n";
#endif
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

		scs.checkAliveTimerTask = env.taskScheduler().scheduleDelayedTask(CHECK_ALIVE_TASK_TIMER_INTERVAL, (TaskFunc*) sendLivenessCommandHandler, rtspClient);
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
	ourRTSPClient* client = (ourRTSPClient*)rtspClient;
	StreamClientState& scs = client->scs;
	char const* rtspUrl = strDup(rtspClient->url());
	char const* rtmpUrl = strDup(client->endpoint());
	
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

		Medium::close((ourRTMPClient*)client->publisher);
		client->publisher = NULL;
	}

	env << *rtspClient << "Closing the stream.\n";
	Medium::close(rtspClient);

	if (--rtspClientCount == 0) {
		//exit(exitCode);
	}

	openURL(env, rtspUrl, rtmpUrl);
}

void subsessionAfterPlaying(void* clientData) {
	MediaSubsession* subsession = (MediaSubsession*) clientData;
	RTSPClient* rtspClient = (RTSPClient*) (subsession->miscPtr);

	Medium::close(subsession->sink);
	subsession->sink = NULL;

	MediaSession& session = subsession->parentSession();
	MediaSubsessionIterator iter(session);
	while ((subsession = iter.next()) != NULL) {
		if (subsession->sink != NULL) return;
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

void usage(UsageEnvironment& env) {
	env << "Usage: " << progName << " <rtsp-url-1> ... <rtsp-url-N>\n";
	env << "\t(where each <rtsp-url-i> is a \"rtsp://\" URL)\n";
}

void announceStream(RTSPClient* rtspClient) {
	UsageEnvironment& env = rtspClient->envir();
	env << *rtspClient << "Publish the stream. endpoint:\"" << ((ourRTSPClient*) rtspClient)->endpoint() << "\"\n";
}

// Implementation of "ourRTSPClient":
ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env, char const* rtspURL, char const* rtmpURL,
		int verbosityLevel, char const* applicationName, portNumBits tunnelOverHTTPPortNum) {
	return new ourRTSPClient(env, rtspURL, rtmpURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env, char const* rtspURL,  char const* rtmpURL, int verbosityLevel,
		char const* applicationName, portNumBits tunnelOverHTTPPortNum) :
		RTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1) {
	fDestUrl = strDup(rtmpURL);
}

ourRTSPClient::~ourRTSPClient() {
	delete[] fDestUrl;
}

// Implementation of "StreamClientState":
StreamClientState::StreamClientState()
  : iter(NULL), session(NULL), subsession(NULL), streamTimerTask(NULL), checkAliveTimerTask(NULL), duration(0.0) {
}

StreamClientState::~StreamClientState() {
	delete iter;
	if (session != NULL) {
		UsageEnvironment& env = session->envir(); // alias
		env.taskScheduler().unscheduleDelayedTask(checkAliveTimerTask);
		checkAliveTimerTask = NULL; 
		env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
		Medium::close(session);
	}
}

// Implementation of "DummySink":
DummySink* DummySink::createNew(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId) {
	return  new DummySink(env, subsession, streamId);
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession, char const* streamId) 
  : MediaSink(env), fSubsession(subsession),  fHaveWrittenFirstFrame(True), fSps(NULL), fPps(NULL) {
	fStreamId = strDup(streamId);
	fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE];
	rtmpClient = (ourRTMPClient*)((ourRTSPClient*)subsession.miscPtr)->publisher;
}

DummySink::~DummySink() {
	delete[] fReceiveBuffer;
	delete[] fStreamId;
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
	 
	if(fHaveWrittenFirstFrame) {
		if(!isSPS(nal_unit_type) && !isPPS(nal_unit_type) && !isIDR(nal_unit_type))
			goto NEXT;
		else if(isSPS(nal_unit_type)) {
			if (!sendSpsPacket(fReceiveBuffer+4, frameSize, timestamp)) 
				goto RECONNECT;
		} else if(isPPS(nal_unit_type)) {
			if (!sendPpsPacket(fReceiveBuffer+4, frameSize, timestamp))
				goto RECONNECT;
			fHaveWrittenFirstFrame = False;
		} else if(isIDR(nal_unit_type)) {
			//send sdp: sprop-parameter-sets
			if (!rtmpClient->sendH264FramePacket(fSps, fSpsSize, timestamp)) 
				goto RECONNECT;

			if (!rtmpClient->sendH264FramePacket(fPps, fPpsSize, timestamp))
				goto RECONNECT;
			
			fHaveWrittenFirstFrame = False;
		}
		goto NEXT;
	}

	if (strcasecmp(fSubsession.mediumName(), "video" ) == 0 &&
			(isIDR(nal_unit_type) || isNonIDR(nal_unit_type))) {
		fReceiveBuffer[0] = 0;	fReceiveBuffer[1] = 0;
		fReceiveBuffer[2] = 0;	fReceiveBuffer[3] = 1;
		if(!rtmpClient->sendH264FramePacket(fReceiveBuffer, frameSize+4, timestamp))
			goto RECONNECT;
	} 
	goto NEXT;

 RECONNECT:
 	fHaveWrittenFirstFrame = True;
 	ourRTMPClient::createNew(envir(), (ourRTSPClient*) fSubsession.miscPtr);
 NEXT:
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
ourRTMPClient*  ourRTMPClient::createNew(UsageEnvironment& env, RTSPClient* rtspClient) {
	ourRTMPClient* instance = new ourRTMPClient(env, rtspClient); 
	return instance->connect() ? instance : NULL;
}

ourRTMPClient::ourRTMPClient(UsageEnvironment& env, RTSPClient* rtspClient) :
		Medium(env), rtmp(NULL), fTimestamp(0), dts(0), pts(0) {
	fSource = (ourRTSPClient*)rtspClient;
	fSource->publisher = this;
	rtmp = srs_rtmp_create(fSource->endpoint());
}

ourRTMPClient::~ourRTMPClient() {
	envir() << *fSource << "Cleanup when unpublish. client disconnect peer" << "\n";
	srs_rtmp_destroy(rtmp);
}

Boolean ourRTMPClient::connect() {
	do {
		if (srs_rtmp_handshake(rtmp) != 0) {
			envir() << *fSource << "simple handshake failed." << "\n";
			break;
		}
#ifdef DEBUG
		envir() << *fSource <<"simple handshake success" << "\n";
#endif

		if (srs_rtmp_connect_app(rtmp) != 0) {
			envir() << *fSource << "connect vhost/app failed." << "\n";
			break;
		}
#ifdef DEBUG
		envir() << *fSource <<"connect vhost/app success" << "\n";
#endif

		int ret = srs_rtmp_publish_stream(rtmp);
		if (ret != 0) {
			envir() << *fSource << "publish stream failed.(ret=" << ret << ")\n";
			break;
		}
#ifdef DEBUG
		envir() << *fSource << "publish stream success" << "\n";
#endif
		return True;
	} while (0);

	Medium::close(this);
	return False;
}

Boolean ourRTMPClient::sendH264FramePacket(u_int8_t* data, unsigned size, unsigned timestamp) {
	do {
		if(fTimestamp == 0)
			fTimestamp = timestamp;

		pts = dts += (timestamp-fTimestamp);
		fTimestamp = timestamp;

		int ret = srs_h264_write_raw_frames(rtmp, (char*)data, size, dts, pts);
		if (ret != 0) {
			if (srs_h264_is_dvbsp_error(ret)) {
				envir() << *fSource << "ignore drop video error, code=" << ret << "\n";
			} else if (srs_h264_is_duplicated_sps_error(ret)) {
				envir() << *fSource << "ignore duplicated sps, code=" << ret << "\n";
			} else if (srs_h264_is_duplicated_pps_error(ret)) {
				envir() << *fSource << "ignore duplicated pps, code=" << ret << "\n";
			} else {
				envir() << *fSource << "send h264 raw data failed. code=" << ret << "\n";
				break;
			}
		}
#ifdef DEBUG
		u_int8_t nut = data[4] & 0x1F;
		envir() << *fSource << "sent packet: type=video"
		<< ", time=" << dts << ", size=" << size 
		<< ", b[4]=" << (unsigned char*)data[4] << "("
		<< (isSPS(nut)? "SPS" : (isPPS(nut) ? "PPS" : (isIDR(nut) ? "I" : (isNonIDR(nut) ? "P" : "Unknown")))) << ")\n";
#endif
		return True;
	} while(0);
	
	Medium::close(this);
	return False;
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
