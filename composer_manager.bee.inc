<?php

/**
 * @file
 * Bee hook implementations for the Composer Manager module.
 */

/**
 * Implements hook_bee_command().
 */
function composer_manager_bee_command() {
  $items = array();

  // Execute a composer command within the Composer Manager working directory.
  $items['composer-manager'] = array(
    'description' => bt('Executes a Composer command in the directory containing the Composer Manager composer.json file.'),
    'aliases' => array('composer-execute'),
    'bootstrap' => BACKDROP_BOOTSTRAP_FULL,
    'group' => 'composer_manager',
    'arguments' => array(
      'command' => bt('Optional. The composer subcommand to run (e.g., install, update). Defaults to "install".'),
    ),
    'optional_arguments' => array('command'),
    'callback' => 'bee_composer_manager_execute',
  );

  // Rebuild the consolidated composer.json file.
  $items['composer-json-rebuild'] = array(
    'description' => bt('Rebuilds the consolidated composer.json file.'),
    'aliases' => array('composer-rebuild', 'composer-rebuild-file'),
    'bootstrap' => BACKDROP_BOOTSTRAP_FULL,
    'group' => 'composer_manager',
    'callback' => 'bee_composer_manager_composer_json_rebuild',
  );

  return $items;
}

/**
 * Bee callback: Executes a Composer command on Composer Manager's composer.json.
 *
 * @param array $arguments
 *   Bee-provided positional arguments. Accepts optional 'command'.
 * @param array $options
 *   Bee-provided options (unused here, but accepted for forward-compatibility).
 */
function bee_composer_manager_execute($arguments = array(), $options = array()) {
  // Determine working directory for Composer (files/composer by default).
  try {
    $dir = composer_manager_file_dir(TRUE);
  }
  catch (Exception $e) {
    bee_message(bt('Error resolving path to Composer directory: @msg', array('@msg' => $e->getMessage())), 'error');
    return;
  }

  // Normalize path for Windows Composer check below.
  if (strtoupper(substr(PHP_OS, 0, 3)) === 'WIN') {
    $dir = str_replace('\\\\', '/', $dir);
    $dir = str_replace('\\', '/', $dir);
  }

  // Ensure composer.json exists; if not, offer to rebuild.
  if (!file_exists($dir . '/composer.json')) {
    if (function_exists('bee_confirm')) {
      if (!bee_confirm(bt('The composer.json file is missing. Would you like to re-build it now?'))) {
        bee_message(bt('Missing composer.json file. Run "bee composer-json-rebuild" to generate it.'), 'error');
        return;
      }
    }
    // Attempt rebuild.
    $rebuilt = composer_manager_write_file();
    if (!$rebuilt) {
      bee_message(bt('Failed to build composer.json file.'), 'error');
      return;
    }
  }

  // Resolve the composer command to execute.
  $subcommand = 'install';
  if (!empty($arguments) && !empty($arguments['command'])) {
    $subcommand = $arguments['command'];
  }

  // Build the shell command. Pass-through additional args is not yet supported
  // here for simplicity; minimal implementation as requested.
  $cmd_parts = array('composer', $subcommand, '--working-dir=' . escapeshellarg($dir));
  $cmd = implode(' ', $cmd_parts);

  bee_message(bt('Running: @cmd', array('@cmd' => $cmd)), 'log');

  // Execute composer and stream output to the console.
  // Use passthru if available to stream; otherwise fallback to exec.
  $exit_code = 0;
  if (function_exists('passthru')) {
    passthru($cmd, $exit_code);
  }
  else {
    exec($cmd, $output, $exit_code);
    if (!empty($output)) {
      foreach ((array) $output as $line) {
        bee_message($line, 'log');
      }
    }
  }

  if ($exit_code !== 0) {
    bee_message(bt('Composer command failed with exit code @code.', array('@code' => $exit_code)), 'error');
    return;
  }

  // After an install or update, update autoloader and allow modules to react.
  if (in_array($subcommand, array('install', 'update'), TRUE)) {
    try {
      $loader = composer_manager_register_autoloader();
      $composer = composer_manager_vendor_dir() . '/composer';

      // Update classmap, PSR-4, PSR-0, and required files similar to drush impl.
      bee_composer_manager_update_classmap($loader, $composer);
      bee_composer_manager_update_psr4($loader, $composer);
      bee_composer_manager_update_psr0($loader, $composer);
      bee_composer_manager_update_files($composer);

      // Let other modules know dependencies may now be available.
      module_invoke_all('composer_dependencies_install');
    }
    catch (Exception $e) {
      // Non-fatal; libraries may still be usable, but warn the user.
      bee_message(bt('Autoloader update warning: @msg', array('@msg' => $e->getMessage())), 'warning');
    }
  }
}

/**
 * Bee callback: Rebuilds the consolidated composer.json file.
 */
function bee_composer_manager_composer_json_rebuild() {
  $success = composer_manager_write_file();
  if ($success) {
    bee_message(bt('Completed building composer.json file.'), 'success');
  }
  else {
    bee_message(bt('Error building composer.json file.'), 'error');
  }
}

/**
 * Add new class maps to the class loader.
 * Mirrors the Drush implementation for Bee runtime.
 *
 * @param \Composer\Autoload\ClassLoader $loader
 * @param string $composer
 */
function bee_composer_manager_update_classmap(\Composer\Autoload\ClassLoader $loader, $composer) {
  $autoload_classmap = $composer . '/autoload_classmap.php';
  if (file_exists($autoload_classmap)) {
    $class_map = $loader->getClassMap();
    $updated_class_map = require $autoload_classmap;
    $loader->addClassMap(array_diff($updated_class_map, $class_map));
  }
}

/**
 * Add new PSR-4 namespaces to the class loader.
 *
 * @param \Composer\Autoload\ClassLoader $loader
 * @param string $composer
 */
function bee_composer_manager_update_psr4($loader, $composer) {
  $autoload_psr4 = $composer . '/autoload_psr4.php';
  if (file_exists($autoload_psr4)) {
    $updated_psr4 = require $autoload_psr4;
    foreach ($updated_psr4 as $prefix => $paths) {
      $loader->addPsr4($prefix, $paths);
    }
  }
}

/**
 * Add new PSR-0 namespaces to the class loader.
 *
 * @param \Composer\Autoload\ClassLoader $loader
 * @param string $composer
 */
function bee_composer_manager_update_psr0($loader, $composer) {
  $autoload_psr0 = $composer . '/autoload_namespaces.php';
  if (file_exists($autoload_psr0)) {
    $updated_psr0 = require $autoload_psr0;
    foreach ($updated_psr0 as $prefix => $paths) {
      $loader->add($prefix, $paths);
    }
  }
}

/**
 * Require new files added by Composer libraries.
 *
 * @param string $composer
 */
function bee_composer_manager_update_files($composer) {
  $autoload_files = $composer . '/autoload_files.php';
  if (file_exists($autoload_files)) {
    $updated_files = require $autoload_files;
    foreach ($updated_files as $file) {
      if (file_exists($file)) {
        require_once $file;
      }
    }
  }
}
