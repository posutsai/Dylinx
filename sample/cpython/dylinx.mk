include Makefile
DYLINX_LIB_WITH_GLUE = -latomic -lpthread -ldl -ldlx-init -ldlx-glue
DYLINX_LIB_WO_GLUE = -latomic -lpthread -ldl -ldlx-init
python3-dlx:	Programs/python.o $(LIBRARY) $(LDLIBRARY) $(PY3LIBRARY)
	$(LINKCC) $(PY_CORE_LDFLAGS) $(LINKFORSHARED) -o $@ Programs/python.o $(BLDLIBRARY) $(LIBS) $(MODLIBS) $(SYSLIBS) $(DYLINX_LIB_WITH_GLUE)
python3-xray:	Programs/python.o $(LIBRARY) $(LDLIBRARY) $(PY3LIBRARY)
	$(LINKCC) $(PY_CORE_LDFLAGS) $(LINKFORSHARED) -o $@ Programs/python.o ${DYLINX_HOME}/build/.objs/dlx-glue/linux/x86_64/release/src/glue/dylinx-glue.c.o $(BLDLIBRARY) $(LIBS) $(MODLIBS) $(SYSLIBS) $(DYLINX_LIB_WO_GLUE)
