# SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny
# Smirnov (arseny30@gmail.com) 2014-2026 SPDX-FileCopyrightText: Copyright 2026
# telemt community SPDX-License-Identifier: BSL-1.0 AND MIT telemt:
# https://github.com/telemt telemt: https://t.me/telemtrs

if((CMAKE_MAJOR_VERSION LESS 3) OR (CMAKE_VERSION VERSION_LESS "3.10"))
  message(FATAL_ERROR "CMake >= 3.10 is required")
endif()

set(TD_TEST_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/country_info.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_destination_budget_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_destination_budget_controller_rotation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_lifecycle_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_pool_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_rotation_gate_snapshot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_flow_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_flow_controller_rotation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_flow_runtime_policy_reload_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_flow_destination_budget_joint_gate_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_lifecycle_report.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/connection_lifecycle_report_rotation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/active_connection_lifecycle_state_machine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/active_connection_lifecycle_cutover_failure_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/active_connection_lifecycle_runtime_reload_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/session_new_session_resend_scope.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/db.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_phase3_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_phase3_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_phase3_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_phase3_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_vendor_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http_reader_smuggling_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/link.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/message_link_info_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/message_link_info_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/message_link_info_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/message_entities.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/message_entity_v557_pre_code_regression.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/socks5_response_parser_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mtproto.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dns_resolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ip_address_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/buffer_initialization_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dialog_filter_icon_mapping_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/poll.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pvs_regressions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pvs_level1_contracts_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tl_simple_struct_init_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tl_simple_struct_init_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tl_parser_memory_leak_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cli_get_args_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cli_get_args_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_report_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_report_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_report_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_report_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_true_issues_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_true_issues_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_true_issues_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_true_issues_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/security_core_true_issues_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sonar_blocker_wave8_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sonar_blocker_wave8_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sonar_blocker_wave8_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sonar_blocker_wave8_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sonar_blocker_wave8_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sonar_blocker_wave9_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/sonar_blocker_wave9_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secret_api_object_fetch_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secret_api_object_fetch_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secret_api_object_fetch_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secret_api_object_fetch_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secret_api_object_fetch_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/flat_hash_table_memsize_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/flat_hash_table_memsize_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/flat_hash_table_memsize_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/flat_hash_table_memsize_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/flat_hash_table_memsize_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdutils_heap_hash_misc_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdutils_heap_hash_misc_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdutils_heap_hash_misc_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdutils_heap_hash_misc_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdutils_heap_hash_misc_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/td_api_json_const_dispatch_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/td_api_json_const_dispatch_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/td_api_json_const_dispatch_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/td_api_json_const_dispatch_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/td_api_json_const_dispatch_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/query_merger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secret.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/secure_storage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/set_with_position.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/string_cleaning.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mime_type_update_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdclient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tqueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/translation_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_hints_from_country.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_entropy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_parser_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_parser_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_parser_length_fields_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_route_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_route_country_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_route_policy_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_route_fail_closed_equivalence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_route_failure_cache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_padding_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_builder_route_seam.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_extension_order_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_key_share_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_pq_group_sync_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_single_lane_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_context_entropy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_builder_seam.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_ech_enc_key_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_differential.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_hmac_regression.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_circuit_breaker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_profile_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_order_diagnostics_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_diagnostics_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_log_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_generation_log_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_fragmentation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_record_order_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_leftover_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_multi_record_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_multirecord_rejection_matrix_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_route_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_coverage_gaps.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_ech_wire_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_darwin_ech_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_darwin_profile_hardcoding_bug.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_imported_corpus_runtime_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_secret_sni_boundary_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_secret_sni_boundary_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_circuit_breaker_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_circuit_breaker_reload_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_cb_ttl_reconcile_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_registry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_registry_provenance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_platform_coherence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_platform_defaults.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_alps_extension_wire_type_invariants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ping_proxy_lifetime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ip_preference_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ip_preference_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ip_preference_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ip_preference_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_dual_stack_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_dual_stack_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_dual_stack_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_route_integrity_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_route_integrity_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_route_integrity_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_route_integrity_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_route_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ping_main_dc_log_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_tls_init_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_logging_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tdnet_logging_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_raw_ip_transport_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_raw_ip_transport_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_raw_ip_transport_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_raw_ip_route_transport_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dispatcher_transport_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dispatcher_transport_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dispatcher_transport_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dispatcher_policy_sink_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_transport_security_option_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_transport_option_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_auth_manager_result_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_auth_manager_stale_result_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_mux_policy_sink_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_auth_data_session_mode_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_auth_data_legacy_mode_gate_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_mode_policy_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_option_security_immutability_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_policy_signal_isolation_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_mode_policy_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_option_security_immutability_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_policy_signal_isolation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_mode_policy_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_mode_policy_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_mode_policy_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_sink_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_signal_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_signal_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_signal_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_signal_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_mode_map_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_mode_map_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_mode_map_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_mode_map_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_m5_mode_map_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_reauth_barrier_poison_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_reauth_barrier_poison_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_clock_poison_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_clock_poison_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_window_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_window_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_window_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_window_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_window_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q7_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q9_sink_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q9_path_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q9_path_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q9_path_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q9_path_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_q9_path_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_bootstrap_state_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_handover_cutover_failure_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_new_session_resend_scope_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_state_sink_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_protocol_envelope_frame_safety.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_policy_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_online_update_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_entry_count_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_entry_count_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_window_count_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_window_count_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_window_count_duplicate_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_window_count_duplicate_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_window_count_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_blob_store_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_blob_store_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_blob_store_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bundle_to_handshake_flow_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_label_domain_separation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_reference_table_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_reference_table_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_reference_table_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_slot_entry_rejection_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_source_layout_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_entry_window_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_entry_window_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_entry_window_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_main_keyset_lookup_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_main_keyset_catalog_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_h5_catalog_cardinality_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_h5_arc_bridge_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_h5_arc_imported_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_main_keyset_catalog_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_main_keyset_catalog_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_main_keyset_catalog_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_main_keyset_handshake_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_source_layout_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_source_layout_bundle_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_source_layout_key_block_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_source_layout_image_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_channel_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_channel_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_channel_fixture_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_decay_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_decay_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_decay_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_decay_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_auth_destroy_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_auth_destroy_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_auth_destroy_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_config_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_option_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_transfer_sink_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_route_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_route_fixture_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_synergy_bugs_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_reauth_delay_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_auth_recovery_sink_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_decode_sink_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_window_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_window_migrate_token_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_window_migrate_token_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_window_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_bundle_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_bundle_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_bundle_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_bundle_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_bundle_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_catalog_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_catalog_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_catalog_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_catalog_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_catalog_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_bundle_cdn_ingestion_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_bundle_cdn_first_seen_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_bundle_cdn_ingestion_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_encapsulated_key_validity_invariants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_firefox_macos_psk_extension_invariants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_grease_key_share_entry_invariants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_pq_hybrid_key_share_format_invariants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_secp256r1_key_share_irng_determinism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_profile_spec_pq_consistency_invariants.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_capture_chrome144_differential.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_capture_firefox148_differential.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_capture_safari26_3_differential.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_cross_family_one_bit_differentiators.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_route_ech_quic_block_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_extension_order_template_catalog_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_wire_pattern_distinguisher_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_wire_feature_mask_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_wire_real_fixture_features_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/WireClassifierFeatures.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/UpstreamRuleVerifiers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/FamilyLaneMatchers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/ReviewedClientHelloReferences.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_upstream_rule_verifiers_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_upstream_rule_verifiers_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_family_lane_matcher_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_family_lane_matcher_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_reviewed_family_lane_baselines_coverage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_firefox_fixture_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_firefox_linux_desktop_ech.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_route_failure_cache_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_route_failure_cache_expiry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_builder_equivalence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_real_fixture_alignment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_runtime_profile_coverage_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_params_platform_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_active_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_transition_timing_fingerprint_stability_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_sticky_rotation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_destination_key_safety.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_destination_key_case_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_destination_key_case_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_case_alias_fixture_blackbox.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_selection_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_builder_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_release_profile_gating_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_transport_confidence_gate_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_transport_confidence_unknown_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_transport_confidence_unknown_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_transport_confidence_unknown_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_transport_confidence_unknown_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_transport_confidence_unknown_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_platform_weight_gate_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_platform_weight_gate_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_platform_weight_gate_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_platform_weight_gate_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_platform_weight_gate_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_cross_os_profile_rejection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_platform_lane_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_and_transport_gate_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_proxy_alpn_semantics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_plan.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_drift_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_drift_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_drift_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_drift_multiloader_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_drift_multiloader_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_drift_multiloader_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_drift_multiloader_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_drift_multiloader_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_cooldown.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_ancestor_security_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_relative_ancestor_security_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_filesystem_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_reload_log_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_wakeup_seam.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_wakeup_overdue_shaping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_flush_order.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_error_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_quick_ack_log_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_crypto_padding_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_shaping_wakeup_seam.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_hints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_handshake_bootstrap_hints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_handshake_error_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_handshake_logging_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_bulk_threshold_runtime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_bulk_threshold_max_runtime_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_ack_threshold_runtime_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_ack_min_threshold_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_ack_truncation_runtime_drift_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_inbound_ack_container_drift_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_record_size_sequence_hardening_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_record_size_sequence_hardening_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_auth_recovery_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_auth_recovery_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_auth_recovery_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_auth_recovery_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_hint_wiring.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_hint_control_mix_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_event_bounds_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_event_bounds_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_event_bounds_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_event_bounds_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_hint_wiring_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_parse_error_diagnostics_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_service_ack_mix_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_phase_hint_drift_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ping_connection_hint_wiring.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ping_connection_parse_error_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_route_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ping_proxy_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_classification_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_backoff_overflow_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_fallback_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_log_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_log_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_enforcement_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_time_clamp_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_enforcement_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_retry_cap_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_direct_retry_no_backoff_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_retry_failure_log_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ping_proxy_log_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dialog_filter_get_chat_folder_hardening_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dialog_filter_get_chat_folder_hardening_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_td_and_messages_logging_hardening_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_secret_tls_domain_validation_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_secret_sni_boundary_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_secret_tls_domain_validation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_input_validation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_contract_mutation_smoke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_contract_concurrency_chaos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_socket_routing_behavioral.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_secret_tls_truncation_boundary_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_mtproto_secret_roundtrip_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_serialized_secret_validation_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dc_option_secret_validation_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_recovery_proxy_secret_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ping_proxy_inheritance_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_setup_error_emitters_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_rejection_retry_harness_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_rejection_retry_classification_matrix_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_rejection_classification_determinism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_rejection_retry_soak.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_lane_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_lane_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_lane_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_lane_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_lane_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_control_plane_lane_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_control_plane_lane_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_window_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_window_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_window_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_window_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_window_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_salt_policy_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_salt_policy_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_salt_policy_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_salt_policy_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_salt_policy_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_peer_channel_guard_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_peer_channel_guard_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_peer_channel_guard_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_gate_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_gate_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_gate_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_peer_channel_guard_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_peer_channel_guard_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_clear_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_clear_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_clear_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_clear_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_clear_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_gate_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_gate_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_entry_gate_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_protocol_guard_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_protocol_guard_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_protocol_guard_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_protocol_guard_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_lane_protocol_guard_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_flow_anchor_reset_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_flow_anchor_reset_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_flow_anchor_reset_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_flow_anchor_reset_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_flow_anchor_reset_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_manager_recovery_policy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_manager_recovery_policy_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_profile_record_limit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_profile_record_limit_consistency.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_nonfinite_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_overflow_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_record_padding_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_runtime_params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_runtime_defaults_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_params_flow_behavior_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_profile_policy_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_darwin_profile_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_darwin_profile_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_darwin_profile_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_hint_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_inner_composition.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_partial_drain_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_hint_lifecycle_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_distribution.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_jitter_distribution.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_seed_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_soak.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_phase_overhead_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_overhead_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_first_write_overhead_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_partial_budget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_zero_write_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_camouflage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_runtime_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_policy_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_drs_prime_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_repeated_prime_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_transition_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_contention_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_contention_soak.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_mixed_hint_contention_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_backlog_matrix_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_cap_boundary_matrix_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_post_handshake_greeting_override_cross_matrix_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_greeting_model_overflow_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_greeting_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_size_correlation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_size_correlation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_size_correlation_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_size_correlation_greeting_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_size_correlation_wire_size_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_size_correlation_unknown_hint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_size_correlation_unknown_hint_greeting.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_size_correlation_chaff_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_zero_length_chaff_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_response_jitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_response_jitter_queue_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_response_jitter_keepalive_interleave_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_response_jitter_non_head_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_response_jitter_zero_length_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_bidirectional_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_budget_boundaries.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_inbound_activity_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_seed_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_wakeup_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_budget_oversized_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_small_record_budget_integration_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_payload_overhead_budget_integration_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_unsatisfiable_overhead_wakeup_integration_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_chaff_scheduler_cumulative_resume_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_chaff_scheduler_unsatisfiable_wakeup_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_scheduler_budget_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_scheduler_time_fail_closed_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_chaff_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_chaff_model_overflow_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_chaff_nonfinite_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_chaff_overflow_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_record_size_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_record_padding_budget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_budget_interactions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_wakeup_edge_cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_clock_anomaly_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_manual_override_unsupported_transport_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_contract_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_overflow_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_statistics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_bypass_state_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_state_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_precision_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_bulkdata_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_idle_fast_path.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_fairness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_wakeup_fairness_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_auth_handshake_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_precision_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_storm_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_storm_randomized.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_storm_soak.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_storm_seed_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_arrival_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_cross_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine_bulkdata_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine_zero_write_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_plateau_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_state_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_prime_idle_skew.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_jitter_range_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_weighted_skew.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_weighted_skew_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_seed_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_connection_count_cap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_connection_count_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_connection_count_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_connection_count_hardening.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_connection_count_overflow_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_alpn_connection_coherence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_packet_buffer_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_shaper_ring_buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_shaper_ring_buffer_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_shaper_ring_buffer_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_activation_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_seam.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_transport_integration_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_transport_runtime_threshold_reload_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_record_size_sequence_hardening_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_primer_transition_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_distribution_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_jitter_distribution_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_soak_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_drs_weight_overflow_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_drs_jitter_overflow_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_payload_cap_inversion_fail_closed.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_sizing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_target.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_target_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_deep_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_small_budget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_mtproto_crypto_padding_bucket_elimination.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_mtproto_crypto_padding_bucket_elimination_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_mtproto_crypto_padding_bucket_elimination_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_traffic_classifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_traffic_classifier_threshold_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_traffic_classifier_service_ack_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_traffic_classifier_ack_mix_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_traffic_classifier_ack_flood_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_traffic_classifier_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_traffic_classifier_query_bytes_boundary_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_ack_mix_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_grease_independence_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_mlkem_coefficient_distribution.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_extension_order_entropy_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_total_length_distribution_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_chrome_extension_set_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_chrome_grease_uniformity_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_chrome_ech_payload_uniformity_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_chrome_permutation_position_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_alps_type_consistency_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_grease_slot_independence_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_firefox_invariance_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_firefox_macos_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_ja3_ja4_stability_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_safari26_3_invariance_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_ios_apple_tls_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_ios_chromium_gap_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_android_chromium_alps_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_android_chromium_no_alps_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_wire_size_distribution_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_statistical_sampling_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_fixed_mobile_profile_invariance_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_cross_platform_contamination_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_hmac_timestamp_adversarial_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_grease_autocorrelation_adversarial_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_cross_platform_contamination_extended_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_corpus_structural_key_material_stress_1k.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_android_chromium_alps_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_android_chromium_no_alps_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_ios_apple_tls_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_ios_chromium_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_ios_chromium_stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_linux_chrome_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_linux_chrome_stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_linux_firefox_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_linux_firefox_stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_macos_firefox_baseline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_macos_firefox_stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_ios_apple_tls_stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_windows_chrome_stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_multi_dump_windows_firefox_stats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_nightly_wire_baseline_monte_carlo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_nightly_boundary_falsification.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_nightly_cross_family_distance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_record_size_baselines.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_builder_length_agreement_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_handshake_frame_domain_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_cross_connection_fingerprint_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_hmac_timing_security.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_all_profiles_random_offset_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_hmac_binary_secret_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_padding_boundary_edge_cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_builder_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_structural_differential.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_ech_circuit_breaker_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_ja4_fingerprint_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_cipher_suite_order_determinism.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_alpn_presence_all_profiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_chrome_extension_set_invariance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_server_hello_parser_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_client_hello_acceptance_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_handshake_acceptance_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_handshake_acceptance_route_lanes.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_full_handshake_family_pairing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_first_flight_layout_pairing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_verified_profile_serverhello_pairing_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_serverhello_fixture_loader_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_serverhello_fixture_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_serverhello_pairing_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_serverhello_pairing_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_serverhello_pairing_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_grease_structural_positions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_structural_invariants_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hmac_replay_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_ja4_segment_a_correctness.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_ja3_ja4_cross_validation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_wire_image_deep_safety.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_transport_wire_parser_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_transport_wire_memory_bounds.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_authentication_constant_time.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_encoder_frame_protocol_coverage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_frame_length_envelope_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_channel_key_activation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_policy_ech_coverage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_hash_divergence_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_codec_channel_concurrency_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_wire_image_memory_safety_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine_monotone_saturation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_padding_policy_wire_size_overshoot_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_chaff_scheduler_budget_bypass_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_traffic_classifier_size_max_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_bucket_boundary_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_bucket_boundary_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_bucket_boundary_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_bucket_boundary_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_counter_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_case_alias_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_fqdn_alias_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_legacy_fqdn_alias_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_lookup_budget_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_store_key_helper_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_legacy_migration_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_legacy_migration_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_legacy_migration_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_legacy_migration_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_legacy_migration_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_legacy_precedence_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_box_muller_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_hints_country_boundary_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_fingerprint_classifier_blackhat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_profile_selection_weight_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_circuit_breaker_concurrency_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_serialization_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_numeric_overflow_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_parse_fail_ttl_race_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_route_failure_policy_reload_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_sticky_rotation_route_reload_storm_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_anchor_overflow_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ech_cb_success_reset_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_ech_downstream_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_profile_weight_bridge_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_profile_weight_bridge_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_profile_weight_bridge_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_profile_weight_bridge_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_profile_sticky_rotation_reload_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_tls_init_profile_temporal_divergence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_proxy_alpn_all_profiles_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_ech_cb_temporal_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_traffic_classifier_contract_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_ipt_controller_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_idle_sampler_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_padding_entropy_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_profile_platform_isolation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_psk_ech_coupling_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_ios_ech_disabled_fingerprint_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_masking_proxy_alpn_length_compensation_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_alert_injection_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_manual_override_bidir_floor_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_backpressure_drs_idle_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_chaff_ipt_interaction_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_greeting_bidir_floor_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_ring_capacity_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_hint_lifecycle_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_full_integration_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_subsystem_integration_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_slot_dispatch_noop_source_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_slot_dispatch_noop_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bind_state_machine_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bind_state_machine_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_monitor_counter_overflow_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_loading_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_loading_positive.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_loading_negative.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_loading_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_loading_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_loading_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_loading_light_fuzz_extended.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_trust_store_integrity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_apple_trust_store_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_apple_trust_store_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_apple_trust_store_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_apple_trust_store_edge_cases.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_host_latch_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_host_latch_unit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_host_latch_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_host_latch_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_host_latch_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_host_latch_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tdnet/test_ssl_ctx_cert_load_openssl3_contract.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/data.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/data.h
    ${TDUTILS_TEST_SOURCE}
    ${TDACTOR_TEST_SOURCE})
set(TD_TEST_SOURCE
    ${TD_TEST_SOURCE}
    PARENT_SCOPE)

set(TESTS_MAIN main.cpp)

# add_library(all_tests STATIC ${TD_TEST_SOURCE})
# target_include_directories(all_tests PUBLIC
# $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
# target_link_libraries(all_tests PRIVATE tdcore tdclient)

if(NOT CMAKE_CROSSCOMPILING OR EMSCRIPTEN)
  if(OPENSSL_FOUND)
    add_executable(test-crypto EXCLUDE_FROM_ALL crypto.cpp)
    target_include_directories(test-crypto SYSTEM
                               PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(
      test-crypto PRIVATE ${OPENSSL_CRYPTO_LIBRARY} ${CMAKE_DL_LIBS}
                          ${ZLIB_LIBRARIES} tdutils tdmtproto)

    if(WIN32)
      if(MINGW)
        target_link_libraries(test-crypto PRIVATE ws2_32 mswsock crypt32)
      else()
        target_link_libraries(test-crypto PRIVATE ws2_32 Mswsock Crypt32)
      endif()
    endif()
  endif()

  add_executable(test-tdutils EXCLUDE_FROM_ALL ${TESTS_MAIN}
                                               ${TDUTILS_TEST_SOURCE})
  add_executable(test-online EXCLUDE_FROM_ALL online.cpp)
  add_executable(
    run_session_record_size_sequence_hardening_adversarial_tests
    EXCLUDE_FROM_ALL
    ${TESTS_MAIN}
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_record_size_sequence_hardening_adversarial.cpp
  )
  add_executable(
    run_record_size_sequence_hardening_tests EXCLUDE_FROM_ALL
    ${TESTS_MAIN}
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs_distribution.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine_bulkdata_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_drs_engine_zero_write_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_adversarial.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_plateau_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_state_edges.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_prime_idle_skew.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_jitter_range_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_weighted_skew.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_weighted_skew_stress.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_record_size_sequence_hardening_integration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_record_size_sequence_hardening_wire.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_light_fuzz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_record_size_sequence_hardening_seed_matrix.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_primer_transition_wire.cpp
  )
  add_executable(run_all_tests ${TESTS_MAIN} ${TD_TEST_SOURCE})
  if(CLANG
     AND NOT CYGWIN
     AND NOT EMSCRIPTEN
     AND NOT (CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
     AND NOT (CMAKE_SIZEOF_VOID_P EQUAL 4))
    target_compile_options(test-tdutils PUBLIC -fsanitize=undefined
                                               -fno-sanitize=vptr)
    target_compile_options(
      run_session_record_size_sequence_hardening_adversarial_tests
      PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(run_record_size_sequence_hardening_tests
                           PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(run_all_tests PUBLIC -fsanitize=undefined
                                                -fno-sanitize=vptr)
    target_compile_options(test-online PUBLIC -fsanitize=undefined
                                              -fno-sanitize=vptr)
    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined -fno-sanitize=vptr")
  endif()
  target_include_directories(
    run_session_record_size_sequence_hardening_adversarial_tests
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_include_directories(
    run_record_size_sequence_hardening_tests
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_include_directories(
    run_all_tests PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  # Point repo-relative fixture loaders (ServerHelloFixtureLoader) at the source
  # tree so tests can read reviewed JSON artifacts regardless of the build-time
  # working directory.
  target_compile_definitions(
    run_all_tests PRIVATE "TELEMT_TEST_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\"")
  if(TD_ENABLE_TARGET_PCH AND COMMAND target_precompile_headers)
    target_precompile_headers(run_all_tests PRIVATE
                              ${CMAKE_SOURCE_DIR}/test/test_pch.h)
  endif()
  # `test/stealth/test_source_layout_contract.cpp` verifies the source-level
  # layout of the retained PEM blobs by re-parsing them with OpenSSL's
  # `PEM_read_bio_RSA_PUBKEY` / `d2i_RSAPublicKey`. The public transitive
  # include chain for `run_all_tests` doesn't re-export OpenSSL headers, so the
  # test TU needs its own SYSTEM include of the vcpkg / system OpenSSL headers.
  # `OPENSSL_INCLUDE_DIR` is already resolved by the top-level
  # `find_package(OpenSSL ...)` call.
  if(OPENSSL_FOUND)
    target_include_directories(run_all_tests SYSTEM
                               PRIVATE ${OPENSSL_INCLUDE_DIR})
  endif()
  target_include_directories(
    test-tdutils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_link_libraries(test-tdutils PRIVATE tdutils)
  target_link_libraries(
    run_session_record_size_sequence_hardening_adversarial_tests
    PRIVATE tdcore tdclient)
  target_link_libraries(run_record_size_sequence_hardening_tests
                        PRIVATE tdcore tdclient)
  target_link_libraries(run_all_tests PRIVATE tdutils tdactor tdclient tdcore
                                              tdmtproto tdnet)
  target_link_libraries(test-online PRIVATE tdcore tdjson_private tdclient
                                            tdutils tdactor)

  if(CLANG)
    # add_executable(fuzz_url fuzz_url.cpp) target_link_libraries(fuzz_url
    # PRIVATE tdcore) target_compile_options(fuzz_url PRIVATE
    # "-fsanitize-coverage=trace-pc-guard")
  endif()

  set(TD_RUN_ALL_TESTS_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/run_all_tests")
  if(EXISTS "${TD_RUN_ALL_TESTS_EXECUTABLE}")
    execute_process(
      COMMAND "${TD_RUN_ALL_TESTS_EXECUTABLE}" --list
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      RESULT_VARIABLE TD_RUN_ALL_TESTS_DISCOVERY_RESULT
      OUTPUT_VARIABLE TD_RUN_ALL_TESTS_DISCOVERY_OUTPUT
      ERROR_VARIABLE TD_RUN_ALL_TESTS_DISCOVERY_ERROR
      OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(TD_RUN_ALL_TESTS_DISCOVERY_RESULT EQUAL 0)
      set(TD_RUN_ALL_TESTS_DISCOVERY_TEXT
          "${TD_RUN_ALL_TESTS_DISCOVERY_OUTPUT}\n${TD_RUN_ALL_TESTS_DISCOVERY_ERROR}"
      )
      string(REPLACE "\r\n" "\n" TD_RUN_ALL_TESTS_DISCOVERY_TEXT
                     "${TD_RUN_ALL_TESTS_DISCOVERY_TEXT}")
      string(REPLACE "\n" ";" TD_RUN_ALL_TESTS_CASES
                     "${TD_RUN_ALL_TESTS_DISCOVERY_TEXT}")
      foreach(TD_RUN_ALL_TEST_NAME IN LISTS TD_RUN_ALL_TESTS_CASES)
        if(TD_RUN_ALL_TEST_NAME STREQUAL "")
          continue()
        endif()
        if(TD_RUN_ALL_TEST_NAME MATCHES
           "^Selected [0-9]+ of [0-9]+ registered tests$")
          continue()
        endif()
        if(TD_RUN_ALL_TEST_NAME MATCHES "^libgcov profiling error:")
          continue()
        endif()

        add_test(NAME "${TD_RUN_ALL_TEST_NAME}"
                 COMMAND run_all_tests --exact "${TD_RUN_ALL_TEST_NAME}")
        set_tests_properties("${TD_RUN_ALL_TEST_NAME}"
                             PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        if(TD_RUN_ALL_TEST_NAME
           MATCHES
           "^(Test_DB_key_value|Test_DB_key_value_set_all|Test_SecureStorage_secret|Test_Misc_update_atime_change_atime|Test_Misc_update_atime_saves_mtime|Test_FamilyLaneMatcherContract_ReviewedTableByIndexCountMatches|Test_HmacTimestampAdversarial1k_AllProfilesSameSeedSameTimestampProduceDifferentClientRandom|Test_AuthenticationTimingConsistency_StringComparisonNoEarlyExit)$"
        )
          set_tests_properties("${TD_RUN_ALL_TEST_NAME}" PROPERTIES RUN_SERIAL
                                                                    TRUE)
        endif()

        # Tests that create database/filesystem artefacts at their working
        # directory must run from CMAKE_BINARY_DIR (not CMAKE_SOURCE_DIR) so: 1.
        # Temporary files don't pollute the source tree. 2. Stale files from
        # interrupted runs don't cause "readonly database" failures because the
        # build tree is always writable. 3. The test's own "destroy before open"
        # preamble reliably cleans up.
        if(TD_RUN_ALL_TEST_NAME
           MATCHES
           "^(Test_DB_key_value|Test_DB_key_value_set_all|Test_DB_binlog_key_value|Test_DB_thread_key_value|Test_DB_persistent_key_value|Test_SecureStorage_secret|Test_Misc_update_atime)"
        )
          set_tests_properties(
            "${TD_RUN_ALL_TEST_NAME}" PROPERTIES WORKING_DIRECTORY
                                                 "${CMAKE_BINARY_DIR}")
        endif()

        set(TD_RUN_ALL_TEST_LABELS "")
        if(TD_RUN_ALL_TEST_NAME
           MATCHES
           "(Corpus|MultiDump|WireClassifierBlackhat|WirePatternDistinguisherContract|WireFeatureMaskContract|WireRealFixtureFeaturesContract|HandshakeAcceptance|FullHandshakeFamilyPairing|FirstFlightLayoutPairing|CrossFamilyOneBitDifferentiators|RouteEchQuicBlockMatrix)"
        )
          list(APPEND TD_RUN_ALL_TEST_LABELS "STEALTH_CORPUS_QUICK")
        endif()
        if(TD_RUN_ALL_TEST_NAME MATCHES "Nightly")
          list(APPEND TD_RUN_ALL_TEST_LABELS "STEALTH_CORPUS_FULL")
        endif()
        if(TD_RUN_ALL_TEST_LABELS)
          list(REMOVE_DUPLICATES TD_RUN_ALL_TEST_LABELS)
          set_tests_properties("${TD_RUN_ALL_TEST_NAME}"
                               PROPERTIES LABELS "${TD_RUN_ALL_TEST_LABELS}")
        endif()
      endforeach()
    else()
      message(
        WARNING
          "Failed to discover run_all_tests cases: ${TD_RUN_ALL_TESTS_DISCOVERY_ERROR}"
      )
      add_test(run_all_tests run_all_tests)
    endif()
  else()
    message(
      STATUS
        "run_all_tests executable is not available during configure; registering umbrella CTest entry"
    )
    add_test(run_all_tests run_all_tests)
  endif()
endif()
