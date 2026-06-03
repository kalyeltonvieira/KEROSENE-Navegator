CC ?= gcc
CXX ?= g++
CARGO ?= cargo
WINDRES ?= windres

APP := kerosene.exe
BUILD := build
RUST_LIB := target/release/libkerosene_ui.a

CFLAGS := -std=c11 -O2 -DNDEBUG -DWIN32_LEAN_AND_MEAN -DCOBJMACROS -Wall -Wextra -Wno-unused-parameter
CXXFLAGS := -std=c++17 -O2 -DNDEBUG -DWIN32_LEAN_AND_MEAN -Wall -Wextra -Wno-unused-parameter -Ideps/build/native/include
LDFLAGS := -mwindows
LIBS := -ld2d1 -ldwrite -ld3d11 -ldxgi -lwinhttp -lole32 -luuid -lshlwapi -lcomctl32 -lgdi32 -luser32 -lshell32 -ladvapi32 -lws2_32 -lbcrypt -luserenv -lntdll
WEBVIEW2_LIB := deps/build/native/x64/WebView2Loader.dll.lib
WEBVIEW2_DLL := deps/build/native/x64/WebView2Loader.dll

SRC := \
	src/main.c \
	src/cyclone/parser_html.c \
	src/cyclone/parser_css.c \
	src/cyclone/layout_engine.c \
	src/cyclone/box_model.c \
	src/cyclone/flexbox.c \
	src/cyclone/grid.c \
	src/cyclone/paint.c \
	src/spark/runtime.c \
	src/spark/compiler.c \
	src/spark/jit.c \
	src/spark/gc.c \
	src/ignite/d3d11_renderer.c \
	src/ignite/compositor.c \
	src/ignite/text_atlas.c \
	src/ignite/shader_compiler.c \
	src/net/http_client.c \
	src/net/quic_handler.c \
	src/net/search.c \
	src/net/cache.c \
	src/platform/win32_window.c \
	src/platform/directwrite.c \
	src/platform/file_io.c

CPP_SRC := src/platform/webview2_host.cpp
RC_SRC := src/resources/kerosene.rc

OBJ := $(SRC:src/%.c=$(BUILD)/%.o)
CPP_OBJ := $(CPP_SRC:src/%.cpp=$(BUILD)/%.o)
RES_OBJ := $(BUILD)/resources/kerosene.res.o

.PHONY: all clean run rust

all: $(APP)

rust: $(RUST_LIB)

$(RUST_LIB): Cargo.toml src/ui/lib.rs
	$(CARGO) build --release

$(APP): $(OBJ) $(CPP_OBJ) $(RES_OBJ) $(RUST_LIB)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(CPP_OBJ) $(RES_OBJ) $(RUST_LIB) $(WEBVIEW2_LIB) $(LIBS)
	powershell -NoProfile -Command "Copy-Item -LiteralPath '$(WEBVIEW2_DLL)' -Destination 'WebView2Loader.dll' -Force"

$(BUILD)/%.o: src/%.c src/kerosene.h
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) $(CFLAGS) -Isrc -c $< -o $@

$(BUILD)/%.o: src/%.cpp src/kerosene.h
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

$(RES_OBJ): $(RC_SRC) src/resource.h src/resources/kerosene.ico
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(WINDRES) -Isrc $< -O coff -o $@

run: $(APP)
	.\$(APP)

clean:
	@if exist "$(BUILD)" rmdir /s /q "$(BUILD)"
	@if exist "$(APP)" del "$(APP)"
	@if exist "WebView2Loader.dll" del "WebView2Loader.dll"
	$(CARGO) clean
