CXX ?= g++
CXXFLAGS ?= -std=gnu++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS := -I../include

.PHONY: all test integration runtime runtime-visible validate validate-visible clean

all: core_state_test infra_test integration_test wii_control_harness_test

COMMON_CORE := ../core/app_core.cpp ../core/control_schema.cpp ../core/config.cpp \
               ../core/error.cpp ../core/persistence.cpp ../core/http_client.cpp \
               ../core/session.cpp ../core/api_client.cpp ../core/downloader.cpp \
               ../core/runtime_sync.cpp

core_state_test: core_state_test.cpp $(COMMON_CORE)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

infra_test: infra_test.cpp $(COMMON_CORE)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

integration_test: integration_test.cpp $(COMMON_CORE)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

wii_control_harness_test: wii_control_harness_test.cpp $(COMMON_CORE)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

integration: integration_test
	./run_integration_test.sh

test: core_state_test infra_test integration wii_control_harness_test
	./core_state_test
	./infra_test
	./wii_control_harness_test

runtime:
	../scripts/test-runtime-emulators.sh

runtime-visible:
	RUNTIME_HEADLESS=0 ../scripts/test-runtime-emulators.sh

validate: test runtime

validate-visible: test runtime-visible

clean:
	rm -f core_state_test infra_test integration_test wii_control_harness_test
