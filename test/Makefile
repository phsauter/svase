# Copyright (c) 2022 ETH Zurich and University of Bologna.
# Licensed under the Apache License, Version 2.0, see LICENSE for details.
# SPDX-License-Identifier: Apache-2.0

# Author:  Paul Scheffler <paulsc@iis.ee.ethz.ch>

SVASE     ?= ../build/svase
build_dir ?= build

all: simple-tests

SIMPLE_DIRS      := param assign
SIMPLE_FILES     := $(foreach dir,$(SIMPLE_DIRS),$(wildcard $(dir)/*.sv))

## run small tests for different transformations
simple-tests:
	mkdir -p debug
	@echo "$(SIMPLE_FILES)"
	@for file in $(SIMPLE_FILES); do \
		printf "\nRunning svase on $$file...\n"; \
		base=$$(basename $$file); \
		$(SVASE) test debug/$${base%.*}_out.sv $$file || { echo "Error: Test $$base failed"; exit 1; }; \
	done

pickles: test_deps

test_deps:
	cargo install bender morty
	pip install mako hjson jsonref jsonschema

$(build_dir):
	mkdir -p $@

# include snitch.mk
# include idma.mk
# include cva6.mk
# include cheshire.mk
