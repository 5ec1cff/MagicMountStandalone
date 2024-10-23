# Magic Mount Standalone

The extracted version of [Magisk](https://github.com/topjohnwu/Magisk) Magic Mount

## Build

./gradlew zipDebug
./gradlew zipRelease

Package will be placed at app/release, including executables and debug symbols

## Install for test

./gradlew installDebug
./gradlew installRelease

Binaries will be push to /data/local/tmp/magic_mount

## Usage

```shell
magic_mount <mount|umount> [--work-dir dir] [--magic magic] [--add-partitions /p1,/p2,....]

mount: do magic mount
umount: umount all magic mounts

magic: the name of the work dir
work-dir: the path of the work dir
add-partitions: add special partitions to mount
```
