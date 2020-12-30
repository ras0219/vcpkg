# Tool Ports

Tool ports are ports that must be built for the host architecture in order to function. These ports usually contain executables, such as code generators or build systems. When another port depends on a tool port in its dependencies list, the tool port will be built for the current host triplet instead of the current depending code's triplet.

## Declaring a Tool Port

A tool port is declared by specifying `"tool"` for the `"type"` field in the manifest. For example:

```json
{
    "name": "contoso-dbuild",
    "version-string": "1.0.0",
    "type": "tool",
    "description": "Contoso's declarative build system",
    "dependencies": [
        "zlib"
    ]
}
```

## Specifying the Host Triplet

The default host triplets are chosen based on the host architecture and operating system, for example `x64-windows`, `x64-linux`, or `x64-osx`. They can be overridden via the `VCPKG_DEFAULT_HOST_TRIPLET` environment variable or via the command line flag `--host-triplet=...`.
