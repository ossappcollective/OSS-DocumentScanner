fastlane documentation
----

# Installation

Make sure you have the latest version of the Xcode command line tools installed:

```sh
xcode-select --install
```

For _fastlane_ installation instructions, see [Installing _fastlane_](https://docs.fastlane.tools/#installing-fastlane)

# Available Actions

### test

```sh
[bundle exec] fastlane test
```

test lane

### checkGitStatus

```sh
[bundle exec] fastlane checkGitStatus
```

Check Git Status

### checkSentryClitatus

```sh
[bundle exec] fastlane checkSentryClitatus
```

Check Sentry Cli Status

### ns_install

```sh
[bundle exec] fastlane ns_install
```

Ensure deps are installed

### setup

```sh
[bundle exec] fastlane setup
```

Setup

### run_sh

```sh
[bundle exec] fastlane run_sh
```

Run command in fastlane "env"

### prepare

```sh
[bundle exec] fastlane prepare
```

Prepare the application.

### build_and_publish

```sh
[bundle exec] fastlane build_and_publish
```



### build_flavor

```sh
[bundle exec] fastlane build_flavor
```



### svelteCheck

```sh
[bundle exec] fastlane svelteCheck
```



### get_changelog

```sh
[bundle exec] fastlane get_changelog
```



----


## iOS

### ios sentry_upload

```sh
[bundle exec] fastlane ios sentry_upload
```

Upload Dsyms to Sentry

### ios upload_store

```sh
[bundle exec] fastlane ios upload_store
```



### ios certificates

```sh
[bundle exec] fastlane ios certificates
```

Fetch certificates and provisioning profiles

### ios build

```sh
[bundle exec] fastlane ios build
```

Build the iOS application.

### ios beta

```sh
[bundle exec] fastlane ios beta
```

Ship iOS build to Testflight.

### ios alpha

```sh
[bundle exec] fastlane ios alpha
```

Ship iOS build to Testflight.

### ios production

```sh
[bundle exec] fastlane ios production
```

Ship iOS build to AppStore.

----


## Android

### android sentry_upload

```sh
[bundle exec] fastlane android sentry_upload
```

Upload  to Sentry

### android write_changelog

```sh
[bundle exec] fastlane android write_changelog
```



### android upload_store

```sh
[bundle exec] fastlane android upload_store
```



### android build

```sh
[bundle exec] fastlane android build
```

Build the Android application.

### android github

```sh
[bundle exec] fastlane android github
```

Ship to Github.

### android fdroid

```sh
[bundle exec] fastlane android fdroid
```

build for fdroid.

### android beta

```sh
[bundle exec] fastlane android beta
```

Ship to Android Playstore Beta.

### android alpha

```sh
[bundle exec] fastlane android alpha
```

Ship to Android Playstore Alpha.

----

This README.md is auto-generated and will be re-generated every time [_fastlane_](https://fastlane.tools) is run.

More information about _fastlane_ can be found on [fastlane.tools](https://fastlane.tools).

The documentation of _fastlane_ can be found on [docs.fastlane.tools](https://docs.fastlane.tools).
