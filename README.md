# Crayons
A simple image annotator written in GTK.


## Building
### Requirements

- Fedora
	```sh
	sudo dnf install gcc pkgconf-pkg-config gtk2-devel gtk3-devel
	```
- Arch
	```sh
	sudo pacman -S base-devel gtk2 gtk3
	```
- Ubuntu/Debian
	```sh
	sudo apt update && sudo apt install build-essential pkg-config libgtk2.0-dev libgtk-3-dev
	```

## Usage
```sh
./crayons <path_to_image>
# or
./crayons # opens an empty window
```

## License
Licensed under the [Mozilla Public License v2.0](LICENSE)
