#ifndef __TEST_SRS_LIBRTMP_HH_
#define __TEST_SRS_LIBRTMP_HH_

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "BitVector.hh"
#include "srs_librtmp.h"

#define RTSP_CLIENT_VERBOSITY_LEVEL 0

//MAX width*height*1.5  .e.g  IDR -> 1280x720x1.5
#define DUMMY_SINK_RECEIVE_BUFFER_SIZE  (1024*1024+512*1024)

#define CHECK_ALIVE_TASK_TIMER_INTERVAL 30*1000*1000

//#define DEBUG

void openURL(UsageEnvironment& env, char const* rtspURL, char const* rtmpUrl);

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);
void setupNextSubsession(RTSPClient* rtspClient);
void shutdownStream(RTSPClient* rtspClient, int exitCode = 1);

void subsessionAfterPlaying(void* clientData);
void subsessionByeHandler(void* clientData);
void streamTimerHandler(void* clientData);
void sendLivenessCommandHandler(void* clientData);

void usage(UsageEnvironment& env);

void announceStream(RTSPClient* rtspClient);
void addStartCode(u_int8_t*& dest, u_int8_t* src, unsigned size);
//void analyze_h264_seq_parameter_set_data(u_int8_t* sps, unsigned spsSize, unsigned& profile_idc, unsigned& level_idc, unsigned& width, unsigned& height, unsigned& fps);

 //Sequence parameter set
#define isSPS(nut)              (nut == 7)
//Picture parameter set
#define isPPS(nut)              (nut == 8)
//Supplemental enhancement information
#define isSEI(nut)              (nut == 6)
//Access unit delimiter
#define isAUD(nut)              (nut == 9)
//Coded slice of an IDR picture
#define isIDR(nut)              (nut == 5)
//Coded slice of a non-IDR picture
#define isNonIDR(nut)         (nut == 1)

class StreamClientState {
public:
        StreamClientState();
        virtual ~StreamClientState();

public:
        MediaSubsessionIterator* iter;
        MediaSession* session;
        MediaSubsession* subsession;
        TaskToken streamTimerTask;
        TaskToken checkAliveTimerTask;
        double duration;
        void* rtmpClient;
};

class ourRTSPClient: public RTSPClient {
public:
        static ourRTSPClient* createNew(UsageEnvironment& env, char const* rtspURL,
                        int verbosityLevel = 0, char const* applicationName = NULL,
                        portNumBits tunnelOverHTTPPortNum = 0);

protected:
        ourRTSPClient(UsageEnvironment& env, char const* rtspURL,
                        int verbosityLevel, char const* applicationName,
                        portNumBits tunnelOverHTTPPortNum);

        virtual ~ourRTSPClient();
public:
        StreamClientState scs;
};

class ourRTMPClient {
public:
        static ourRTMPClient* createNew(UsageEnvironment& env, char const* rtmpUrl);
protected:
        ourRTMPClient(UsageEnvironment& env, char const* rtmpUrl);
        virtual ~ourRTMPClient();
public:
        void sendH264FramePacket(UsageEnvironment& env, u_int8_t* data, unsigned size, unsigned timestamp);
        void destroy(UsageEnvironment& env);
        Boolean isConnected;
        char const* url() const { return fUrl; }
private:
        srs_rtmp_t rtmp;
        unsigned hTimestamp;
        u_int32_t dts, pts;
        char* fUrl;
};

class DummySink: public MediaSink {
public:
        static DummySink* createNew(UsageEnvironment& env, MediaSubsession& subsession, ourRTMPClient& client);
private:
        DummySink(UsageEnvironment& env, MediaSubsession& subsession, ourRTMPClient& client);
        // called only by "createNew()"
        virtual ~DummySink();

        static void afterGettingFrame(void* clientData, unsigned frameSize,
                        unsigned numTruncatedBytes, struct timeval presentationTime,
                        unsigned durationInMicroseconds);

        void afterGettingFrame(unsigned frameSize, unsigned numTruncatedBytes,
                        struct timeval presentationTime, unsigned durationInMicroseconds);

private:
        // redefined virtual functions:
        virtual Boolean continuePlaying();
public:
        u_int8_t* sps;
        u_int8_t* pps;
        unsigned spsSize;
        unsigned ppsSize;
private:
        u_int8_t* fReceiveBuffer;
        Boolean fHaveWrittenFirstFrame;
        MediaSubsession& fSubsession;
        ourRTMPClient& rtmpClient;
};

UsageEnvironment& operator << (UsageEnvironment& env, const RTSPClient& rtspClient) {
        return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

UsageEnvironment& operator<< (UsageEnvironment& env, const MediaSubsession& subsession) {
        return env << subsession.mediumName() << "/" << subsession.codecName();
}

UsageEnvironment& operator<< (UsageEnvironment& env, const ourRTMPClient& rtmpClient) {
        return env << "[URL:\"" << rtmpClient.url() << "\"]: ";
}

#endif /* __TEST_SRS_LIBRTMP_HH_ */
