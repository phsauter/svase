# Copyright (c) 2022 ETH Zurich and University of Bologna.
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0

# Author:  Paul Scheffler <paulsc@iis.ee.ethz.ch>

all: install_fmt
all: install_mimalloc
all: install_slang
all: install_CLI11


install:
	mkdir -p $@

install_fmt: | install
	git submodule update --init --recursive fmt
	cd fmt && cmake -B build
	cmake --build fmt/build -j$(shell nproc)
	cmake --install fmt/build --prefix install

install_mimalloc: | install
	git submodule update --init --recursive mimalloc
	cd mimalloc && cmake -B out
	cmake --build mimalloc/out -j$(shell nproc)
	cmake --install mimalloc/out --prefix install

install_slang: | install
	cd slang && cmake -B build
	cmake -S slang -B slang/build -DSLANG_USE_MIMALLOC=OFF
	cmake --build slang/build -j$(shell nproc)
	cmake --install slang/build --prefix install

install_CLI11: | install
	git submodule update --init --recursive CLI11
