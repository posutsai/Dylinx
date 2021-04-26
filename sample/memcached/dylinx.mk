include Makefile
DYLINX_LIB_WITH_GLUE = -L$(DYLINX_HOME)/build/lib -L.dylinx/lib -latomic -lpthread -ldl -ldlx-init -ldlx-glue -fxray-instrument
DYLINX_LIB_WO_GLUE = -latomic -lpthread -ldl -ldlx-init
DEFAULT_INCLUDES = -I. -I${DYLINX_HOME}/src/glue -I.dylinx/glue -fxray-instrument
memcached-xray: $(memcached_OBJECTS) $(memcached_DEPENDENCIES) $(EXTRA_memcached_DEPENDENCIES)
	$(AM_V_CCLD)$(LINK) $(memcached_OBJECTS) ${DYLINX_HOME}/build/.objs/dlx-glue/linux/x86_64/release/src/glue/dylinx-glue.c.o $(memcached_LDADD) $(LIBS) $(DYLINX_LIB_WITH_GLUE)
memcached-dlx: $(memcached_OBJECTS) $(memcached_DEPENDENCIES) $(EXTRA_memcached_DEPENDENCIES)
	$(AM_V_CCLD)$(LINK) $(memcached_OBJECTS) $(memcached_LDADD) $(LIBS) $(DYLINX_LIB_WITH_GLUE)
