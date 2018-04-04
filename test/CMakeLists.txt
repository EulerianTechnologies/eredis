
ADD_EXECUTABLE (test-async test-async.c)
TARGET_LINK_LIBRARIES (test-async eredis)

ADD_EXECUTABLE (test-async-thr test-async-thr.c)
TARGET_LINK_LIBRARIES (test-async-thr eredis)

ADD_EXECUTABLE (test-sync test-sync.c)
TARGET_LINK_LIBRARIES (test-sync eredis)

ADD_EXECUTABLE (test-sync-thr test-sync-thr.c)
TARGET_LINK_LIBRARIES (test-sync-thr eredis)

ADD_EXECUTABLE (eredis-drop-noexpire eredis-drop-noexpire.c)
TARGET_LINK_LIBRARIES (eredis-drop-noexpire eredis)
