{
  "targets": [{
      "target_name" : "node_nvr_addon",
      "sources" : [ "../src/testSrslibrtmp.cpp" , "../src/cJSON.c", "../src/cMD5.cpp" ],
      "defines" : [
          "NODE_ADDON",
          "SOCKLEN_T=socklen_t",
          "LOCALE_NOT_USED",
          "NO_SSTREAM=1",
          "_LARGEFILE_SOURCE=1",
          "_FILE_OFFSET_BITS=64",
          "BSD=1"
      ],
      "cflags" :  [
          " -Wno-reorder"
      ],
      "include_dirs" : [
          "../src",
           "../live555/liveMedia/include",
           "../live555/groupsock/include",
           "../live555/BasicUsageEnvironment/include",
           "../live555/UsageEnvironment/include",
           "../srs-librtmp/objs/include"
      ],
      "libraries" : [
           "../../live555/liveMedia/libliveMedia.a",
           "../../live555/groupsock/libgroupsock.a",
           "../../live555/BasicUsageEnvironment/libBasicUsageEnvironment.a",
           "../../live555/UsageEnvironment/libUsageEnvironment.a",
           "../../srs-librtmp/objs/lib/srs_librtmp.a"
      ]
    }]
}