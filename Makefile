CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
SRC     := src/buf.c src/csv.c src/overrides.c src/convert.c
INC     := -Iinclude -Isrc

all: build/sg2gr

build/sg2gr: $(SRC) cli/main.c include/sg2gr.h src/internal.h | build
	$(CC) $(CFLAGS) $(INC) $(SRC) cli/main.c -o $@

build/test: $(SRC) tests/test.c include/sg2gr.h src/internal.h | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined $(INC) $(SRC) tests/test.c -o $@

test: build/test
	./build/test

# WebAssembly: freestanding, no libc (wasm/shim.c supplies malloc and mem*/strlen).
# Needs clang with the wasm32 target and wasm-ld.
WASM_CC ?= clang
WASM_EXPORTS := sg2gr_wasm_convert sg2gr_alloc sg2gr_free sg2gr_version sg2gr_overrides_parse \
                sg2gr_overrides_free sg2gr_convert
build/sg2gr.wasm: $(SRC) wasm/shim.c wasm/glue.c include/sg2gr.h src/internal.h | build
	$(WASM_CC) --target=wasm32 -std=c99 -O2 -ffreestanding -fno-builtin -nostdlib \
	    -DSG2GR_FREESTANDING $(INC) $(SRC) wasm/glue.c wasm/shim.c -o $@ \
	    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=262144 \
	    -Wl,--export=sg2gr_wasm_convert $(addprefix -Wl$(comma)--export=,$(filter-out sg2gr_wasm_convert,$(WASM_EXPORTS)))

comma := ,

wasm: build/sg2gr.wasm

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all test wasm clean

# Static web app, assembled into public/. public/ is committed because the hosting build
# image has no clang to build the wasm; run `make web` and commit it after changing the C code.
SITE := public
web: build/sg2gr.wasm
	mkdir -p $(SITE)
	cp web/index.html web/style.css web/app.js wasm/sg2gr.mjs build/sg2gr.wasm $(SITE)/
	cp -R web/fonts $(SITE)/

serve: web
	@echo "http://localhost:8000"
	cd $(SITE) && python3 -m http.server 8000

PREFIX ?= /usr/local
install: build/sg2gr
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 build/sg2gr $(DESTDIR)$(PREFIX)/bin/sg2gr
.PHONY: install web serve
