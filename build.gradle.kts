import com.android.build.gradle.LibraryExtension

plugins {
    alias(libs.plugins.agp.lib) apply false
}

fun Project.configureBaseExtension() {
    extensions.findByType(LibraryExtension::class)?.run {
        namespace = "io.github.a13e300.magic_mount_standalone"
        compileSdk = 34
        ndkVersion = "27.0.12077973"
        buildToolsVersion = "34.0.0"

        defaultConfig {
            minSdk = 25
        }

        lint {
            checkReleaseBuilds = false
            abortOnError = true
        }
    }

}

subprojects {
    plugins.withId("com.android.library") {
        configureBaseExtension()
    }
    plugins.withType(JavaPlugin::class.java) {
        extensions.configure(JavaPluginExtension::class.java) {
            sourceCompatibility = JavaVersion.VERSION_17
            targetCompatibility = JavaVersion.VERSION_17
        }
    }
}
