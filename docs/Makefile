# The project README.md is generated from the documentation pages by executing `make` on this directory.

README=../README.md

$(README): index.md installation.md usage.md configuration.md adding.md
	echo '# Overwitch' > $(README)
	echo >> $(README)
	echo '[//]: # (Do not modify this file manually.)' >> $(README)
	echo '[//]: # (This file is generated from the docs directory by executing `make`.)' >> $(README)
	tail -n +4 index.md | sed "s^images/screenshot.png^docs/images/screenshot.png^" >> $(README)
	tail -n +6 installation.md >> $(README)
	tail -n +6 usage.md >> $(README)
	tail -n +6 configuration.md >> $(README)
	tail -n +6 adding.md | grep -v '{% raw %}' | grep -v '{% endraw %}' >> $(README)
