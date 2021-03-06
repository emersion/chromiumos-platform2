{
  'target_defaults': {
    'defines': [
      'USE_CONTAINERS=<(USE_containers)',
      'USE_CFM_ENABLED_DEVICE=<(USE_cfm_enabled_device)',
    ],
    'variables': {
      'deps': [
        'dbus-1',
        'libbrillo-<(libbase_ver)',
        'libchrome-<(libbase_ver)',
        'libudev',
        'libusb-1.0',
      ],
    },
  },
  'targets': [
    {
      'target_name': 'permission_broker_adaptors',
      'type': 'none',
      'variables': {
        'dbus_service_config': 'dbus_bindings/dbus-service-config.json',
        'dbus_adaptors_out_dir': 'include/permission_broker/dbus_adaptors',
        'new_fd_bindings': 1,
      },
      'sources': [
        'dbus_bindings/org.chromium.PermissionBroker.xml',
      ],
      'includes': ['../common-mk/generate-dbus-adaptors.gypi'],
    },
    {
      'target_name': 'libpermission_broker',
      'type': 'static_library',
      'dependencies': ['permission_broker_adaptors'],
      'link_settings': {
        'libraries': [
          '-lpolicy-<(libbase_ver)',
        ],
      },
      'sources': [
        'allow_group_tty_device_rule.cc',
        'allow_hidraw_device_rule.cc',
        'allow_tty_device_rule.cc',
        'allow_usb_device_rule.cc',
        'deny_claimed_hidraw_device_rule.cc',
        'deny_claimed_usb_device_rule.cc',
        'deny_group_tty_device_rule.cc',
        'deny_uninitialized_device_rule.cc',
        'deny_unsafe_hidraw_device_rule.cc',
        'deny_usb_device_class_rule.cc',
        'deny_usb_vendor_id_rule.cc',
        'firewall.cc',
        'hidraw_subsystem_udev_rule.cc',
        'libusb_wrapper.cc',
        'permission_broker.cc',
        'port_tracker.cc',
        'rule.cc',
        'rule_engine.cc',
        'tty_subsystem_udev_rule.cc',
        'udev_scopers.cc',
        'usb_control.cc',
        'usb_driver_tracker.cc',
        'usb_subsystem_udev_rule.cc',
      ],
      'conditions': [
        ['USE_containers == 1', {
          'dependencies': [
            '../container_utils/container_utils.gyp:libdevice_jail',
          ],
        }],
      ],
    },
    {
      'target_name': 'permission_broker',
      'type': 'executable',
      'dependencies': ['libpermission_broker'],
      'sources': ['permission_broker_main.cc'],
    },
  ],
  'conditions': [
    ['USE_test == 1', {
      'targets': [
        {
          'target_name': 'permission_broker_test',
          'type': 'executable',
          'includes': ['../common-mk/common_test.gypi'],
          'dependencies': [
            'libpermission_broker',
            '../common-mk/testrunner.gyp:testrunner',
          ],
          'variables': {
            'deps': [
              'libbrillo-test-<(libbase_ver)',
              'libchrome-test-<(libbase_ver)',
            ],
          },
          'sources': [
            'allow_tty_device_rule_test.cc',
            'allow_usb_device_rule_test.cc',
            'deny_claimed_hidraw_device_rule_test.cc',
            'deny_claimed_usb_device_rule_test.cc',
            'deny_unsafe_hidraw_device_rule_test.cc',
            'deny_usb_device_class_rule_test.cc',
            'deny_usb_vendor_id_rule_test.cc',
            'fake_libusb_wrapper.cc',
            'firewall_test.cc',
            'group_tty_device_rule_test.cc',
            'mock_firewall.cc',
            'port_tracker_test.cc',
            'rule_engine_test.cc',
            'rule_test.cc',
            'usb_control_test.cc',
          ],
        },
      ],
    }],
    # Fuzzer target.
    ['USE_fuzzer == 1', {
      'targets': [
        {
          'target_name': 'firewall_fuzzer',
          'type': 'executable',
          'includes': [
            '../common-mk/common_fuzzer.gypi',
          ],
          'dependencies': [
            'libpermission_broker',
          ],
          'variables': {
            'deps': [
              'libchrome-test-<(libbase_ver)',
            ],
          },
          'sources': [
            'firewall_fuzzer.cc',
          ],
        },
      ],
    }],
  ],
}
