<?php

/**
 * @file
 * Hooks provided by the Composer Manager module.
 */

/**
 * @addtogroup hooks
 * @{
 */

/**
 * Allow modules to alter the consolidated JSON array.
 *
 * @param array &$json
 *   The consolidated JSON compiled from each module's composer.json file.
 */
function hook_composer_json_alter(&$json) {
  $json['minimum-stability'] = 'dev';
}

/**
 * Allow modules to alter the JSON mappings.
 *
 * @param array $map
 *   An associative array of key/value pairs, containing:
 *   - 'properties': (string[]|array[]) An indexed array of whitelisted
 *     property strings to be merged into the compiled Composer JSON file. If
 *     the specified property is an array, it will be treated a "parents" array
 *     to retrieve a nested value, see backdrop_array_get_nested_value().
 *   - 'relative_paths': (array) An associative array of key/value pairs,
 *     containing:
 *     - 'keys': (string[]|array[]) An indexed array of property strings that
 *       will be iterated over to transform its keys into relative paths. If
 *       the specified property is an array, it will be treated a "parents"
 *       array to retrieve a nested value, see backdrop_array_get_nested_value().
 *     - 'values': (string[]|array[]) An indexed array of property strings that
 *       will be iterated over to transform its values into relative paths. If
 *       the specified property is an array, it will be treated a "parents"
 *       array to retrieve a nested value, see backdrop_array_get_nested_value().
 *
 * @see composer_manager_build_json()
 */
function hook_composer_json_map_alter(array &$map) {
  // NOTE: the following code is just for example. These values are already
  // added to the JSON map by default and do not need to be specified again.

  // Whitelist a specific top level property.
  $map['properties'][] = 'config';

  // Whitelist a specific sub-property (e.g. not the whole "extra" property).
  $map['properties'][] = array('extra', 'installer-paths');

  // Let composer manager know that a specific property keys are paths and
  // should be converted into relative paths from the generated Composer JSON.
  $map['relative_paths']['keys'][] = array('extra', 'installer-paths');

  // Let composer manager know that a specific property values are paths and
  // should be converted into relative paths from the generated Composer JSON.
  $map['relative_paths']['values'][] = array('extra', 'patches');
}

/**
 * Allow modules to perform tasks after a composer install has been completed.
 */
function hook_composer_dependencies_install() {
  // Tasks that require a composer install to have been performed.
}

/**
 * @} End of "addtogroup hooks".
 */
