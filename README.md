# Crayons
A simple image annotator written in GTK.

## Screenshots
<img width="732" height="673" alt="Screenshot at 2026-01-11 06-48-34" src="https://github.com/user-attachments/assets/50d05b7b-52bb-4ec8-96f4-3c1ab393496d" />
<img width="802" height="670" alt="Screenshot at 2026-01-11 06-46-15" src="https://github.com/user-attachments/assets/ac561d66-cc46-4b5a-903d-33dd8c1bd586" />

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
