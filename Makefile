# Make file to generate documentation

OPTS    					?= -c . -W
SPHINXBUILD   				?= sphinx-build
SOURCE     					= docs
OUT      					= build

# Put it first so that "make" without argument is like "make help".
help:
	@$(SPHINXBUILD) -M help "$(SOURCE)" "$(OUT)" $(OPTS) $(O)

.PHONY: help Makefile clean all

clean:
	rm -rf "$(OUT)"/*

all: html multiversion

# Catch-all target: route all unknown targets to Sphinx using the new
# "make mode" option.  $(O) is meant as a shortcut for $(OPTS).
%: Makefile
	@$(SPHINXBUILD) -M $@ "$(SOURCE)" "$(OUT)/latest" $(OPTS) $(O)

livehtml:
	sphinx-autobuild "$(SOURCE)" "$(OUT)/latest/html" $(OPTS) \
		--host 0.0.0.0 --port 8000 --open-browser

multiversion:
	sphinx-multiversion ${OPTS} "$(SOURCE)" "$(OUT)/versions"
	python3 tools/versions/make_latest.py "$(OUT)/versions"