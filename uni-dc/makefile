
TARGET := uni-dc

#PLATFORM=PC
#PLATFORM=ARM64

ifeq ($(PLATFORM),PC)
CC=gcc
CXX=g++
AR=ar
STRIP=strip
CFLAGS =-g -O0 -Wall -std=gnu99
CPPFLAGS = -g -O0 -std=c++11 -I/usr/local/include
LIBS := -Wl,-Bstatic -Wl,-Bdynamic -lpthread -lm -ldl -lrt -lboost_system -lboost_date_time -lboost_filesystem -lboost_thread

else ifeq ($(PLATFORM),ARM64)
CC=/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-rockchip1031-linux-gnu-gcc
CXX=/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-rockchip1031-linux-gnu-g++
AR=/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-rockchip1031-linux-gnu-ar
STRIP=/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-rockchip1031-linux-gnu-strip
CFLAGS =-g -O3 -Wall -std=gnu99
CPPFLAGS = -g -O3 -std=c++11 -DPLATFORM_AARCH64 -DTARGET_APP
LIBS := -Wl,-Bstatic -Wl,-Bdynamic -lpthread -lm -ldl -lrt

endif

CPPFLAGS += $(addprefix -I,$(INCLUDES))
CPPFLAGS += -MMD

# CFLAGS += -DTEST
# CPPFLAGS += -DTEST
#设置头文件路径和库路径?
INCLUDES := .
INCLUDES += ../include
INCLUDES += /usr/local/boost_x86/include
INCLUDES += ../ota
INCLUDES += ../ota/libjson
LIBPATH := .
LIBPATH += ./lib
LIBPATH += /usr/local/lib
LIBPATH += /usr/local/boost_x86
LIBPATH += ../lib
RM := rm -f

#以下部分无需�?�?
CSRCS := $(wildcard *.c) $(wildcard libjson/*.c) $(wildcard libxml/*.c) $(wildcard ../ota/*.c) $(wildcard ../ota/libjson/*.c)
CPPSRCS := $(wildcard *.cpp) $(wildcard libjson/*.cpp) $(wildcard libxml/*.cpp) $(wildcard ../ota/*.cpp) $(wildcard ../ota/libjson/*.cpp)
CCSRCS := $(wildcard *.cc) $(wildcard libjson/*.cc) $(wildcard libxml/*.cc) $(wildcard ../ota/*.cc) $(wildcard ../ota/libjson/*.cc)
COBJS := $(patsubst %.c,%.o,$(CSRCS))
CPPOBJS := $(patsubst %.cpp,%.o,$(CPPSRCS))
CCOBJS := $(patsubst %.cc,%.o,$(CCSRCS))
CDEPS := $(patsubst %.o,%.d,$(COBJS))
CPPDEPS := $(patsubst %.o,%.d,$(CPPOBJS))
CCDEPS := $(patsubst %.o,%.d,$(CCOBJS))

.PHONY : all deps objs clean rebuild
all : $(TARGET)
deps : $(CDEPS) $(CPPDEPS) $(CCDEPS)
	$(CXX) -MM -MMD $(CSRCS) $(CPPSRCS) $(CCSRCS)
objs : $(COBJS) $(CPPOBJS) $(CCOBJS)
clean :
	@$(RM) *.o libjson/*.o libxml/*.o ../ota/*.o ../ota/libjson/*.o
	@$(RM) *.d libjson/*.d libxml/*.d ../ota/*.d ../ota/libjson/*.d
	@$(RM) $(TARGET)
rebuild: clean all 

-include $(CDEPS) $(CPPDEPS) $(CCDEPS)
$(TARGET) : $(COBJS) $(CPPOBJS) $(CCOBJS)
	$(CXX) -o $(TARGET) $(COBJS) $(CPPOBJS) $(CCOBJS) $(addprefix -L,$(LIBPATH)) $(LIBS)
	# $(STRIP) $(TARGET)
	@$(RM) *.o
	@$(RM) *.d