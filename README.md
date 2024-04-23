# Composer Manager

[Composer Manager](https://backdropcms.org/project/composer_manager) provides a
gateway to the larger PHP community by enabling Backdrop modules to more easily
use best-in-breed libraries that are managed by
[Composer](https://getcomposer.org/).

There are [many challenges](#why-cant-you-just--) when using Composer with
Backdrop, so the primary goal of this module is to work around them by wrapping
Composer with common Backdrop workflows so that module developers and site
builders can use the [thousands of standards-compliant, platform agnostic PHP
libraries](https://packagist.org/statistics) with as little friction as
possible.

## Installation

* Install this module using the [official Backdrop CMS instructions](https://backdropcms.org/guide/modules)
* Refer to the [Maintaining Dependencies](#maintaining-dependencies)
  section for installing and updating third-party libraries required by
  contributed modules
* Refer to the [Best Practices](#best-practices) section for recommended module
  configurations according to your environment

## Usage For Site Builders

### Maintaining Dependencies

As modules are enabled and disabled, Composer Manager gathers their requirements
and generates a consolidated `composer.json` file in the "Composer File
Directory" as configured in Composer Manager's settings page. There are two ways
to install and update the contributed modules' dependencies:

#### Automatically With Drush (Recommended)

Using `drush en` and `drush dis` to enable and disable modules respectively will
automatically generate the consolidated `composer.json` file and run the
appropriate Composer commands to install and update the required dependencies.

This technique introduces the least amount of friction with existing workflows
and is strongly recommended.

The following Drush commands are also available:

* `drush composer-json-rebuild`: Force a rebuild of the consolidated
  `composer.json` file
* `drush composer-manager [COMMAND] [OPTIONS]`: Pass through commands to
  Composer, refer to the [cli tool's documentation](https://getcomposer.org/doc/03-cli.md)
  for available commands and options.

#### Manually With Composer

If you do not wish to use Drush, you must manually use Composer's command line
tool to install and update dependencies whenever modules are enabled or
disabled. The following steps illustrate the workflow to maintain the
dependencies required by contributed module:

* Visit `admin/modules` and enable / disable the modules that have dependencies
* Change into the the "Composer File Directory" as configured in Composer
  Manager's settings page which is where the consolidated `composer.json` file
  was generated
* If necessary, [download and install](https://github.com/composer/composer/blob/master/doc/01-basic-usage.md#installation)
  the Composer tool
* Run `php composer.phar install --no-dev` on the command line, replace
  `install` with `update` when updating dependencies

Refer to [Composer's documentaton](https://getcomposer.org/doc/) for more
details on how Composer works.

### Configuring Composer Manager

Visit `admin/config/system/composer-manager/settings` as a user with the
`administer site configuration` permission to configure Composer Manager.

### Best Practices

Unfortunately there is arguably no 80% use case that guides sane defaults. Site
builders will likely have to configure Composer Manager according to their
environment, so this section outlines best practices and techniques to help
guide a sustainable, reliable installation.

#### Recommended Settings

It is recommended to maintain a project structure where the composer files and
`vendor/` directory exist alongside the document root. This can be achieved by
modifying the following options in Composer Manager's settings page.

* Vendor Directory: `../vendor`
* Composer File Directory: `../`

You can also set the options in settings.php by adding the following variables:

```php
$config['composer_manager.settings']['vendor_dir'] = '../vendor';
$config['composer_manager.settings']['file_dir'] = '../';
```

*NOTE:* The recommended settings are not the defaults because we cannot assume
that this structure is viable for all use cases. Furthermore, the "Composer File
Directory" is set to a path we know is writable by the web server so the
automatic building of `composer.json` works out of the box.

#### Multisite

It is recommended that each multisite installation has its own library space
since the dependencies are tied to which modules are enabled or disabled and
can differ between sites. Add the following snippet to `settings.php` to group
the libraries by site in a directory outside of the document root:

```php
// Capture the site dir, e.g. "default", "example.localhost", etc.
$site_dir = basename(__DIR__);
$config['composer_manager.settings']['vendor_dir'] = '../lib/' . $site_dir . '/vendor';
$config['composer_manager.settings']['file_dir'] = '../lib/' . $site_dir;
```

*NOTE:* The `sites/*/` directories may seem like an obvious location for the
libraries, however Backdrop removes write permissions to these directories on
every page load which can cause frustration.

#### Production Environments

Dependencies should be managed in development environments and not in production.
Therefore it is recommended to disable the checkboxes that automatically build
the composer.json file and run Composer commands when enabling or disabling
modules on production environments.

Assuming that you can detect whether the site is in production mode via an
environment variable, adding the following snippet to `settings.php` will
disable the options where appropriate:

```php
// Modify the logic according to your environment.
if (getenv('APP_ENV') == 'prod') {
  $config['composer_manager.settings']['autobuild_file'] = 0;
  $config['composer_manager.settings']['autobuild_packages'] = 0;
}
```

## Usage For Module Maintainers

Module maintainers can use Composer Manager to maintain their dependencies by
creating a `composer.json` file in the module's root directory and adding the
appropriate requirements. Refer to [Composer's documentation](https://getcomposer.org/doc/01-basic-usage.md#composer-json-project-setup)
for details on adding requirements.

It is recommended to use [version ranges](https://getcomposer.org/doc/01-basic-usage.md#package-versions)
and [tilde operators](https://getcomposer.org/doc/01-basic-usage.md#next-significant-release-tilde-operator-)
wherever possible to mitigate dependency conflicts.

You can also implement `hook_composer_json_alter(&$json)` to modify the data
used to build the consolidated `composer.json` file before it is written.

### Maintaining A Soft Dependency On Composer Manager

@todo

### Accessing The ClassLoader Object

Once the autoloader is registered, you can retrieve the ClassLoader object by
calling `\ComposerAutoloaderInitComposerManager::getLoader()`. The following
example uses this technique with Doctrine's Annotations library which requires
access to the loader object.

```php

use Doctrine\Common\Annotations\AnnotationRegistry;

$loader = \ComposerAutoloaderInitComposerManager::getLoader();
AnnotationRegistry::registerLoader(array($loader, 'loadClass'));

```

### Relying on composer manager in .install

Composer manager will automatically handle the autoloader in `hook_init()`, so
modules generally don't have to worry about triggering the autoloader. However
there are occasions where `hook_init()` isn't invoked such as during install and
update.php. If you rely on the autoloader in a .install file, you have to make
sure the autoloader is triggered by running
`composer_manager_register_autoloader()` at the beginning of your update
function or your `hook_install()` implementation.

## Why can't you just ... ?

The problems that Composer Manager solves tend to be more complex than they
first appear. This section addresses some of the common questions that are asked
as to why Composer Manager works the way it does.

### Why can't you just run "composer install" in each module's root directory?

If a module contains a `composer.json` file, running `composer install` in its
root directory will download all requirements and dependencies to `vendor/`
directories with their own autoloaders. Relying on this technique poses multiple
challenges:

* Duplicate library code when modules have the same dependencies
* Unexpected classes being sourced depending on which autoloader is registered
  first
* Potential version conflicts that aren't detected since each installation is
  run in isolation

To highlight the challenges, let's say `module_a` requires
`"guzzle/http": "3.7.*"` and `module_b` requires `"guzzle/service": ">=3.7.0"`.
At the time of this post, running `composer install` in each module's directory
will result in version 3.7.4 of `guzzle/http` being installed in `module_a`'s
`vendor/` directory and version 3.8.1 of `guzzle/service` being installed in
`module_b`'s `vendor/` directory.

Because `guzzle/service` depends on `guzzle/http`, you now have duplicate
installs of `guzzle/http`. Furthermore, each installation uses different
versions of the `guzzle/http` component (3.7.4 for `module_a` and 3.8.1 for
`module_b`). If `module_a`'s autoloader is registered first then you have a
situation where version 3.8.1 of `\Guzzle\Service\Client` extends version 3.7.4
of `\Guzzle\Http\Client`.

#### Composer Manager's Solution

Composer Manager finds all `composer.json` files in each enabled module's root
directory and attempts to gracefully merge them into a consolidated
`composer.json` file. This results in a single vendor directory shared across
all modules which prevents code duplication and version mismatches. For the use
case above, Composer will resolve both `guzzle/http` and `guzzle/service` to
version 3.7.4 which is a more consistent, reliable environment.

#### Challenges With Composer Manager

The challenge of Composer Manager's technique is when multiple modules require
different version of the same package, e.g. `"guzzle/http": "3.7.*"` and
`"guzzle/http": "3.8.*"`. Composer Manager will use the version defined in the
`composer.json` file that is associated with the module with the heaviest weight.
Composer Manager will also flag the potential version conflict in the UI so the
site builder is aware of the inconsistency.

### Why can't you just manually maintain a composer.json file?

Manually maintaining a `composer.json` file provides a single library space that
all modules can share, however relying on this technique poses multiple
challenges:

* Site builder responsible for updating file when modules are enabled or updated
* Dependencies are decoupled from the module's codebase
* Multiple files must be maintained for each multisite installation

#### Composer Manager's Solution

Composer Manager automatically generates the `composer.json` file when modules
are enabled and disabled, and there is an option in the UI with a corresponding
Drush command that can rebuild the consolidated `composer.json` file on demand.
Furthermore, using a Drush based workflow will automatically run the appropriate
composer commands whenever modules are enabled or disabled, so the need to run
Composer commands outside of normal workflows is reduced to module updates.

#### Challenges With Composer Manager

There are multiple challenges posed by Composer Manager's technique:

* Web server needs write access to the composer file directory
* Sane multisite configuration requires environment-specific `settings.php`
  configurations
* Must implement `hook_composer_json_alter()` in a module to modify
  `composer.json`

#### Composer Manager's Solution

Refer to the [Why can't you just manually maintain a composer.json file?](#composer-managers-solution-1)
section.

#### Challenges With Composer Manager

Refer to the [Why can't you just manually maintain a composer.json file?](#challenges-with-composer-manager-1)
section.

## Current Maintainers

 - [Laryn Kragt Bakker](https://github.com/laryn).
 - Collaboration and co-maintainers welcome!

## Credits

 - Ported to Backdrop CMS by [Laryn Kragt Bakker](https://github.com/laryn).
 - Support has been provided by [Aten Design Group](https://aten.io).
 - Maintained for Drupal by [Bojan Živanović](https://www.drupal.org/u/bojanz),
   [Chris Pliakas](https://www.drupal.org/u/cpliakas),
   [Andrew Berry](https://www.drupal.org/u/deviantintegral),
   [Nathaniel Hoag](https://www.drupal.org/u/n8nl), and
   [Richard Burford](https://www.drupal.org/u/psynaptic).

 ## License

This project is GPL v2 software. See the LICENSE.txt file in this directory for
complete text.
