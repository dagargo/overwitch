# The project README.md and other resource files are generated from the documentation pages by executing `make` on this directory.

README=../README.md
CONTRIBUTING=../CONTRIBUTING.md

all: $(README) $(CONTRIBUTING) license.md

$(README): index.md installation.md usage.md configuration.md adding.md
	echo '# Overwitch' > $(README)
	echo >> $(README)
	echo '[//]: # (Do not modify this file manually.)' >> $(README)
	echo '[//]: # (This file is generated from the docs directory by executing `make`.)' >> $(README)
	tail -n +4 index.md | sed "s^images/screenshot.png^docs/images/screenshot.png^" >> $(README)
	tail -n +7 installation.md >> $(README)
	tail -n +7 usage.md >> $(README)
	tail -n +7 configuration.md >> $(README)
	tail -n +7 adding.md | grep -v '{% raw %}' | grep -v '{% endraw %}' >> $(README)

$(CONTRIBUTING): contributing.md
	echo '[//]: # (Do not modify this file manually.)' > $(CONTRIBUTING)
	echo '[//]: # (This file is generated from the docs directory by executing `make clean; make`.)' >> $(CONTRIBUTING)
	tail -n +7 contributing.md | sed "s/^##/#/g" >> $(CONTRIBUTING)
