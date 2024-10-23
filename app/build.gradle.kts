import java.io.ByteArrayOutputStream

plugins {
    alias(libs.plugins.agp.lib)
}

fun String.execute(currentWorkingDir: File = file("./")): String {
    val byteOut = ByteArrayOutputStream()
    project.exec {
        workingDir = currentWorkingDir
        commandLine = split("\\s".toRegex())
        standardOutput = byteOut
    }
    return String(byteOut.toByteArray()).trim()
}

val gitCommitCount = "git rev-list HEAD --count".execute().toInt()
val gitCommitHash = "git rev-parse --verify --short HEAD".execute()

val defaultCFlags = arrayOf(
    "-Wall",
    "-Wno-unused", "-Wno-unused-parameter", "-Wno-vla-cxx-extension",
    "-fno-rtti", "-fno-exceptions",
    "-fno-stack-protector", "-fomit-frame-pointer",
    "-Wno-builtin-macro-redefined", "-D__FILE__=__FILE_NAME__",
)

val releaseFlags = arrayOf(
    "-O3", "-flto",
    "-fvisibility=hidden", "-fvisibility-inlines-hidden",
    "-Wl,--exclude-libs,ALL", "-Wl,--gc-sections",
)

android {
    buildFeatures {
        androidResources = false
        buildConfig = false
        prefab = true
        prefabPublishing = true
    }
    externalNativeBuild.cmake {
        path("src/main/cpp/CMakeLists.txt")
    }

    defaultConfig {
        externalNativeBuild.cmake {
            arguments += "-DANDROID_STL=none"
            cFlags("-std=c18", *defaultCFlags)
            cppFlags("-std=c++20", *defaultCFlags)
        }
    }

    buildTypes {
        forEach {
            it.externalNativeBuild.cmake {
                arguments += "-DDEBUG_SYMBOLS_PATH=${layout.buildDirectory.dir("symbols/${it.name}").get().asFile.absolutePath}"
            }
        }
    }

    prefab {
        register("magic_mount")
    }
}

androidComponents.onVariants { variant ->
    val variantLowered = variant.name.lowercase()
    val variantCapped = variant.name.replaceFirstChar { it.uppercaseChar() }
    afterEvaluate {
        task<Zip>("zip$variantCapped") {
            group = "my"
            archiveFileName.set("magic_mount-$gitCommitHash-$gitCommitCount-$variantLowered.zip")
            destinationDirectory.set(layout.projectDirectory.dir("release").asFile)
            dependsOn("externalNativeBuild$variantCapped")

            into("") {
                from(layout.buildDirectory.dir("intermediates/cmake/$variantLowered/obj"))
                from(layout.buildDirectory.dir("symbols/${variant.buildType}"))
            }
        }

        val executableName = "magic_mount"

        task<Task>("install$variantCapped") {
            group = "my"
            dependsOn("externalNativeBuild$variantCapped")

            val abiList = listOf("armeabi-v7a", "arm64-v8a", "x86_64", "x86")
            doLast {
                val primaryArch = "adb shell getprop ro.product.cpu.abi".execute()
                val arch = "adb shell getprop ro.product.cpu.abilist".execute()
                exec {
                    commandLine = listOf(
                        "adb", "shell", "rm /data/local/tmp/$executableName || su -c 'rm /data/local/tmp/$executableName'")
                    isIgnoreExitValue = true
                }
                arch.split(",").forEach { abi ->
                    if (abi !in abiList) {
                        println("ignore unknown abi $abi")
                        return@forEach
                    }
                    val isPrimary = primaryArch == abi
                    val devPath =
                        "/data/local/tmp/${if (isPrimary) executableName else "${executableName}_$abi"}"
                    exec {
                        workingDir =
                            layout.buildDirectory.dir("intermediates/cmake/$variantLowered/obj/$abi").get().asFile
                        commandLine =
                            listOf(
                                "adb",
                                "push",
                                executableName,
                                devPath
                            )
                    }
                    exec {
                        commandLine = listOf(
                            "adb",
                            "shell",
                            "chmod",
                            "+x",
                            devPath
                        )
                    }
                }
            }
        }
    }
}

dependencies {
    implementation(libs.cxx)
}
