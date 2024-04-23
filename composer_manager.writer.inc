<?php

/**
 * @file
 * Functions related to the creation of the consolidated composer.json file.
 */

/**
 * Fetches the data in each module's composer.json file.
 *
 * @return array
 *
 * @throws \RuntimeException
 */
function composer_manager_fetch_data() {
  $data = array();
  foreach (module_list() as $module) {

    $module_dir = backdrop_get_path('module', $module);
    $composer_file = $module_dir . '/composer.json';
    $args = array('@file' => $composer_file);

    if (!file_exists($composer_file)) {
      continue;
    }

    if (!$filedata = @file_get_contents($composer_file)) {
      throw new \RuntimeException(t('Error reading file: @file', $args));
    }

    if (!$json = @backdrop_json_decode($filedata)) {
      throw new \UnexpectedValueException(t('Expecting contents of file to be valid JSON: @file', $args));
    }

    $data[$module] = $json;
  }
  return $data;
}

/**
 * Builds the JSON array containing the combined requirements of each module's
 * composer.json file.
 *
 * @param array $data
 *   An array of JSON arrays parsed from composer.json files keyed by the module
 *   that defines it. This is usually the return value of the
 *   composer_manager_fetch_data() function.
 *
 * @return array
 *   The consolidated JSON array that will be written to a composer.json file.
 *
 * @throws \RuntimeException
 */
function composer_manager_build_json(array $data) {
  $combined = array(
    'require' => array(),
    'config'  => array(
      'autoloader-suffix' => 'ComposerManager',
    ),
    'prefer-stable' => TRUE,
  );

  $vendor_dir = composer_manager_relative_vendor_dir();
  if (0 !== strlen($vendor_dir) && 'vendor' !== $vendor_dir) {
    $combined['config']['vendor-dir'] = $vendor_dir;
  }

  // Retrieve JSON map.
  static $json_map;
  if (!isset($json_map)) {
    $cid = 'composer_manager:json_map';
    if (($cache = cache_get($cid)) && (isset($cache->data)) && is_array($cache->data)) {
      $json_map = $cache->data;
    }
    else {
      $default_map = array(
        'properties' => array(),
        'relative_paths' => array(
          'keys' => array(),
          'values' => array(),
        ),
      );

      $json_map = array(
        'properties' => array(
          'autoload',
          'autoload-dev',
          'config',
          'conflict',
          'provide',
          'prefer-stable',
          'replace',
          'repositories',
          'require',
          'require-dev',
          'suggest',

          // Only support the following "extra" properties.
          // Installers (https://github.com/composer/installers).
          array('extra', 'installer-paths'),

          // Patches (https://github.com/cweagans/composer-patches).
          array('extra', 'patches'),
          array('extra', 'patches-ignore'),
        ),
        'relative_paths' => array(
          'keys' => array(
            array('extra', 'installer-paths'),
          ),
          'values' => array(
            'autoload',
            'autoload-dev',
            array('extra', 'patches'),
            array('extra', 'patches-ignore'),
          ),
        ),
      );

      // Allow modules to alter JSON map.
      backdrop_alter('composer_json_map', $json_map);

      // Ensure JSON map has default keys so we don't have to do isset() checks.
      $json_map = backdrop_array_merge_deep($default_map, $json_map);

      // Cache JSON map.
      cache_set($cid, $json_map);
    }
  }

  // Iterate over each module's JSON.
  foreach ($data as $module => $json) {
    // Merge in mapped JSON properties from the module.
    foreach ($json_map['properties'] as $parents) {
      $parents = (array) $parents;

      // Retrieve value from the module's JSON and skip if it doesn't exist.
      $value = backdrop_array_get_nested_value($json, $parents, $key_exists);
      if (!$key_exists) {
        continue;
      }

      // Retrieve the existing data.
      $existing = backdrop_array_get_nested_value($combined, $parents, $key_exists);

      // If existing value is an array, type cast module data and merge it in.
      if (isset($existing) && is_array($existing)) {
        $value = backdrop_array_merge_deep($existing, (array) $value);
      }

      // Set the value.
      backdrop_array_set_nested_value($combined, $parents, $value, TRUE);
    }

    // Fix property keys and values that contain relative paths.
    foreach ($json_map['relative_paths'] as $type => $data) {
      foreach ($data as $parents) {
        $parents = (array) $parents;
        $property = &backdrop_array_get_nested_value($combined, $parents, $key_exists);
        if (!$key_exists || !is_array($property)) {
          continue;
        }
        composer_manager_relative_json_property($property, array(
          'keys' => $type === 'keys',
          'paths' => array(
            BACKDROP_ROOT . '/' . backdrop_get_path('module', $module),
          ),
        ));
      }
    }

    // Take the lowest stability.
    if (isset($json['minimum-stability'])) {
      if (!isset($combined['minimum-stability']) || composer_manager_compare_stability($json['minimum-stability'], $combined['minimum-stability']) === -1) {
        $combined['minimum-stability'] = $json['minimum-stability'];
      }
    }
  }

  // Allow extensions to alter the composer JSON.
  backdrop_alter('composer_json', $combined);

  return $combined;
}

/**
 * Writes the composer.json file in the specified directory.
 *
 * @param string $dir_uri
 *   The URI of the directory that the composer.json file is being written to.
 *   This is usually the "composer_manager_file_dir" system variable.
 * @param array $json
 *   A model of the JSON data being written. This is usually the return value of
 *   composer_manager_build_json().
 *
 * @throws \RuntimeException
 */
function composer_manager_put_file($dir_uri, array $json) {
  composer_manager_prepare_directory($dir_uri, TRUE);

  // Make the composer.json file human readable for PHP >= 5.4.
  // @see backdrop_json_encode()
  // @see http://drupal.org/node/1943608
  // @see http://drupal.org/node/1948012
  $json_options = JSON_HEX_APOS | JSON_HEX_AMP | JSON_HEX_QUOT;
  if (defined('JSON_PRETTY_PRINT')) {
    $json_options = $json_options | JSON_PRETTY_PRINT;
  }
  if (defined('JSON_UNESCAPED_SLASHES')) {
    $json_options = $json_options | JSON_UNESCAPED_SLASHES;
  }

  $file_uri = $dir_uri . '/composer.json';
  if (!@file_put_contents($file_uri, json_encode($json, $json_options) . PHP_EOL)) {
    throw new \RuntimeException(t('Error writing composer.json file: @file', array('@file' => $file_uri)));
  }

  // Displays a message to admins that a file was written.
  if (user_access('administer site configuration')) {
    $command = file_exists($dir_uri . '/composer.lock') ? 'update' : 'install';
    backdrop_set_message(t('A composer.json file was written to @path.', array('@path' => backdrop_realpath($dir_uri))));
    if ('admin/config/system/composer-manager' != current_path()) {
      $args = array('!command' => $command, '@url' => url('https://backdropcms.org/project/composer_manager', array('absolute' => TRUE)));
      if ('install' == $command) {
        $message = t('Composer\'s <code>!command</code> command must be run to generate the autoloader and install the required packages.<br/>Refer to the instructions on the <a href="@url" target="_blank">Composer Manager project page</a> for installing packages.', $args);
      }
      else {
        $message = t('Composer\'s <code>!command</code> command must be run to install the required packages.<br/>Refer to the instructions on the <a href="@url" target="_blank">Composer Manager project page</a> for updating packages.', $args);
      }
      backdrop_set_message($message, 'warning');
    }
  }
}

/**
 * Ensures the directory is created and protected via .htaccess if necessary.
 *
 * @param string $directory
 *   The URI or path to the directory being prepared.
 * @param bool $for_write
 *   Whether the directory needs write permissions.
 *
 * @return bool
 */
function composer_manager_prepare_directory($directory, $for_write = FALSE) {
  $options = $for_write ? FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS : FILE_CREATE_DIRECTORY;
  if (!file_prepare_directory($directory, $options)) {
    return FALSE;
  }
  if (0 === strpos($directory, 'public://')) {
    file_create_htaccess($directory, TRUE);
  }
  return TRUE;
}

/**
 * Compares the passed minimum stability requirements.
 *
 * @return int
 *   Returns -1 if the first version is lower than the second, 0 if they are
 *   equal, and 1 if the second is lower.
 *
 * @throws \UnexpectedValueException
 */
function composer_manager_compare_stability($a, $b) {
  $number = array(
    'dev' => 0,
    'alpha' => 1,
    'beta' => 2,
    'RC' => 3,
    'rc' => 3,
    'stable' => 4,
  );

  if (!isset($number[$a]) || !isset($number[$b])) {
    throw new \UnexpectedValueException(t('Unexpected value for "minimum-stability"'));
  }

  if ($number[$a] == $number[$b]) {
    return 0;
  }
  else {
    return $number[$a] < $number[$b] ? -1 : 1;
  }
}

/**
 * Helper function for converting value into a relative path.
 *
 * @param mixed $value
 *   The value to change, passed by reference.
 * @param $options
 *   An associative array of additional options, with the following properties:
 *   - 'depth': Maximum depth of recursion.
 *   - 'keys': (bool) Flag indicating whether to iterate over the keys of
 *     $value if it's an array instead of its values.
 *   - 'paths': (string[]) An array of path prefixes to prepend to $value to
 *     check for a valid path.
 *   - 'recurse': When TRUE, the directory scan will recurse the entire tree
 *     starting at the provided directory. Defaults to TRUE.
 *
 * @see composer_manager_build_json()
 */
function composer_manager_relative_json_property(&$value, array $options = array()) {
  static $composer_dir;
  if (!isset($composer_dir)) {
    $composer_dir = composer_manager_file_dir();
  }

  static $vendor_dir;
  if (!isset($vendor_dir)) {
    $vendor_dir = composer_manager_relative_dir(composer_manager_vendor_dir(), $composer_dir);
  }

  static $backdrop_root;
  if (!isset($backdrop_root)) {
    $backdrop_root = composer_manager_relative_dir(BACKDROP_ROOT, $composer_dir);
  }

  $options += array(
    'depth' => 25,
    'keys' => FALSE,
    'paths' => array(),
    'recurse' => TRUE,
  );

  // Recurse through array.
  if ($options['recurse'] && $options['depth'] && is_array($value)) {
    $options['depth']--;
    $new_value = array();
    foreach ($value as $k => &$v) {
      if ($options['keys']) {
        composer_manager_relative_json_property($k, $options);
      }
      else {
        composer_manager_relative_json_property($v, $options);
      }
      $new_value[$k] = $v;
    }
    $value = $new_value;
  }

  // Immediately return if value is not a string.
  if (!is_string($value)) {
    return;
  }

  // Allow and replace a COMPOSER_DIR constant.
  if (strpos($value, 'COMPOSER_DIR') === 0) {
    $value = preg_replace('/\/+/', '/', str_replace('COMPOSER_DIR/', './', $value));
    return;
  }

  // Allow and replace a COMPOSER_VENDOR_DIR constant.
  if (strpos($value, 'COMPOSER_VENDOR_DIR') === 0) {
    $value = preg_replace('/\/+/', '/', str_replace('COMPOSER_VENDOR_DIR/', "$vendor_dir/", $value));
    return;
  }

  // Allow and replace a BACKDROP_ROOT constant.
  if (strpos($value, 'BACKDROP_ROOT') === 0) {
    $value = preg_replace('/\/+/', '/', str_replace('BACKDROP_ROOT', "$backdrop_root/", $value));
    return;
  }

  // Attempt to retrieve an absolute path using the prefixed path and the value.
  $absolute = FALSE;
  foreach ($options['paths'] as $path) {
    $absolute = backdrop_realpath("$path/$value");
    if ($absolute) {
      break;
    }
  }

  // If there still is no valid "to", attempt with just using the value.
  if (!$absolute) {
    $absolute = backdrop_realpath($value);
  }

  // If an absolute path was ascertained, change value to a relative path.
  if ($absolute) {
    $value = composer_manager_relative_dir($absolute, $composer_dir);
  }
}

/**
 * Returns the path for the autoloaded directory or class relative to the
 * directory containing the composer.json file.
 *
 * @deprecated Use composer_manager_relative_json_property() instead.
 */
function composer_manager_relative_autoload_path(&$path, $key, $module) {
  composer_manager_relative_json_property($path, array(
    'paths' => array(
      BACKDROP_ROOT . '/' . backdrop_get_path('module', $module),
    ),
    'recurse' => FALSE,
  ));
}

/**
 * Returns the vendor directory relative to the composer file directory.
 *
 * @throws \RuntimeException
 *   If the vendor or file directories cannot be determined.
 *
 * @return string
 */
function composer_manager_relative_vendor_dir() {
  return composer_manager_relative_dir(
    composer_manager_vendor_dir(),
    composer_manager_file_dir()
  );
}

/**
 * Gets the path of the "to" directory relative to the "from" directory.
 *
 * @param string $dir_to
 *   The absolute path of the directory that the relative path refers to.
 * @param string $dir_from
 *   The absolute path of the directory from which the relative path is being
 *   calculated.
 *
 * @return string
 */
function composer_manager_relative_dir($dir_to, $dir_from) {
  $dirs_to = explode('/', ltrim($dir_to, '/'));
  $dirs_from = explode('/', ltrim($dir_from, '/'));

  // Strip the matching directories so that both arrays are relative to a common
  // position. The count of the $dirs_from array tells us how many levels up we
  // need to traverse from the directory containing the composer.json file, and
  // $dirs_to is relative to the common position.
  foreach ($dirs_to as $pos => $dir) {
    if (!isset($dirs_from[$pos]) || $dirs_to[$pos] != $dirs_from[$pos]) {
      break;
    }
    unset($dirs_to[$pos], $dirs_from[$pos]);
  }

  $path = str_repeat('../', count($dirs_from)) . join('/', $dirs_to);
  if (PHP_OS == 'WINNT'){
    $path = preg_replace('%..\\/([a-zA-Z])%i', '${1}', $path, 1);
  }
  return $path;
}
