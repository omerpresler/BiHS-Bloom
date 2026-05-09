include Makefile.prj.inc
include ../../Makefile.com.inc
include ../../Makefile.exe.inc

TEST_CXXFLAGS = $(PROJ_TEST_CXXFLAGS) $(COMMON_CXXFLAGS) -g
TEST_LNFLAGS = $(PROJ_TEST_LNFLAGS) $(COMMON_LNFLAGS) -g
TEST_OBJS = $(patsubst %.cpp, $(TEST_OBJDIR)/%.o, $(TEST_SRC_CPP))

$(TEST_OBJDIR)/%.o: %.cpp
	@echo "$(notdir $@) (test)"
	mkdir -p $(dir $@)
	$(CXX) -c $(TEST_CXXFLAGS) -o $@ $<

$(TEST_BINDIR)/$(TEST_NAME): $(TEST_OBJS) $(PROJ_TEST_DEP)
	@echo "Linking $@"
	mkdir -p $(dir $@)
	$(LN) -o $@ $(TEST_OBJS) $(PROJ_TEST_LIB) $(TEST_LNFLAGS)

test: $(TEST_BINDIR)/$(TEST_NAME)
	$(TEST_BINDIR)/$(TEST_NAME)

tests: test

.PHONY: test tests
