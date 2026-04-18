# SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
# SPDX-FileCopyrightText: Copyright 2026 telemt community
# SPDX-License-Identifier: BSL-1.0 AND MIT
# telemt: https://github.com/telemt
# telemt: https://t.me/telemtrs

if ((CMAKE_MAJOR_VERSION LESS 3) OR (CMAKE_VERSION VERSION_LESS "3.10"))
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
  ${CMAKE_CURRENT_SOURCE_DIR}/connection_lifecycle_report.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/connection_lifecycle_report_rotation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/active_connection_lifecycle_state_machine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/db.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_phase3_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_phase3_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_phase3_light_fuzz.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_phase3_stress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/sqlite_vendor_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/http.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/link.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/message_entities.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mtproto.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/dns_resolver.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ip_address_security.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/poll.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/query_merger.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/secret.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/secure_storage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/set_with_position.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/string_cleaning.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tdclient.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tqueue.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/translation_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_hints_from_country.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_entropy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_parser_fuzz.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_parser_security.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_leftover_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_multi_record_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_route_stress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_stress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profiles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_coverage_gaps.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_registry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_registry_provenance.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_platform_coherence.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_platform_defaults.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_alps_extension_wire_type_invariants.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ping_proxy_lifetime.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_dual_stack_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_dual_stack_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_dual_stack_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_route_integrity_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_route_integrity_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_route_integrity_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_route_integrity_light_fuzz.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dispatcher_transport_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dispatcher_transport_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_dispatcher_transport_light_fuzz.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_transport_security_option_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_auth_data_session_mode_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_bootstrap_state_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_protocol_envelope_frame_safety.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_policy_source_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_entry_count_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_entry_count_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_window_count_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_window_count_adversarial.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_source_layout_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_source_layout_bundle_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_source_layout_pem_discoverability.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_channel_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_aux_channel_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_light_fuzz.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_stress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_net_monitor_route_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_reauth_delay_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_window_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_route_window_source_contract.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_wire_pattern_distinguisher_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_wire_feature_mask_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_wire_real_fixture_features_contract.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/WireClassifierFeatures.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/UpstreamRuleVerifiers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/FamilyLaneMatchers.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hello_runtime_profile_coverage_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_params.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_params_platform_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_active_policy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_sticky_rotation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_runtime_destination_key_safety.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_proxy_alpn_semantics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_plan.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_platform_runtime.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_cooldown.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_runtime.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_params_loader_security.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_wakeup_seam.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_wakeup_overdue_shaping.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_flush_order.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_crypto_padding_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_shaping_wakeup_seam.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_hints.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_handshake_bootstrap_hints.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_hint_wiring.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_hint_control_mix_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_hint_wiring_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_service_ack_mix_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_phase_hint_drift_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ping_connection_hint_wiring.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_proxy_route_security.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_creator_ping_proxy_security.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_security.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_connection_retry_policy_classification_security.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_setup_error_emitters_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_rejection_retry_harness_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_proxy_rejection_retry_soak.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_config_manager_recovery_policy.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_profile_record_limit.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_nonfinite_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_overflow_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_record_padding_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_runtime_params.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_hint_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_inner_composition.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_partial_drain_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_drs.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_bidirectional_response_jitter_zero_length_edges.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_bidirectional_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_budget_boundaries.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_inbound_activity_edges.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_seed_matrix.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_idle_chaff_traffic_wakeup_edges.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_chaff_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_chaff_nonfinite_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_record_size_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_record_padding_budget.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_padding_budget_interactions.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_wakeup_edge_cases.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_contract_edges.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_overflow_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_statistics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_adversarial.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_shaper_ring_buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_shaper_ring_buffer_stress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_activation_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_seam.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_record_size_sequence_hardening_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_primer_transition_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_distribution_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_jitter_distribution_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_soak_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_drs_weight_overflow_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_drs_jitter_overflow_fail_closed.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_cross_connection_fingerprint_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_hmac_timing_security.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_grease_structural_positions.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_structural_invariants_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_hmac_replay_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_ja4_segment_a_correctness.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_ja3_ja4_cross_validation.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_wire_image_deep_safety.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_transport_wire_parser_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_transport_wire_memory_bounds.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_authentication_constant_time.cpp

  ${CMAKE_CURRENT_SOURCE_DIR}/data.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/data.h

  ${TDUTILS_TEST_SOURCE}
  ${TDACTOR_TEST_SOURCE}
)
set(TD_TEST_SOURCE ${TD_TEST_SOURCE} PARENT_SCOPE)

set(TESTS_MAIN
  main.cpp
)

#add_library(all_tests STATIC ${TD_TEST_SOURCE})
#target_include_directories(all_tests PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
#target_link_libraries(all_tests PRIVATE tdcore tdclient)

if (NOT CMAKE_CROSSCOMPILING OR EMSCRIPTEN)
  if (OPENSSL_FOUND)
    add_executable(test-crypto EXCLUDE_FROM_ALL crypto.cpp)
    target_include_directories(test-crypto SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(test-crypto PRIVATE ${OPENSSL_CRYPTO_LIBRARY} ${CMAKE_DL_LIBS} ${ZLIB_LIBRARIES} tdutils tdmtproto)

    if (WIN32)
      if (MINGW)
        target_link_libraries(test-crypto PRIVATE ws2_32 mswsock crypt32)
      else()
        target_link_libraries(test-crypto PRIVATE ws2_32 Mswsock Crypt32)
      endif()
    endif()
  endif()

  add_executable(test-tdutils EXCLUDE_FROM_ALL ${TESTS_MAIN} ${TDUTILS_TEST_SOURCE})
  add_executable(test-online EXCLUDE_FROM_ALL online.cpp)
  add_executable(run_session_record_size_sequence_hardening_adversarial_tests EXCLUDE_FROM_ALL
                 ${TESTS_MAIN}
                 ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_record_size_sequence_hardening_adversarial.cpp)
  add_executable(run_record_size_sequence_hardening_tests EXCLUDE_FROM_ALL
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
                 ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_drs_primer_transition_wire.cpp)
  add_executable(run_all_tests ${TESTS_MAIN} ${TD_TEST_SOURCE})
  if (CLANG AND NOT CYGWIN AND NOT EMSCRIPTEN AND NOT (CMAKE_SYSTEM_NAME MATCHES "OpenBSD") AND NOT (CMAKE_SIZEOF_VOID_P EQUAL 4))
    target_compile_options(test-tdutils PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(run_session_record_size_sequence_hardening_adversarial_tests PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(run_record_size_sequence_hardening_tests PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(run_all_tests PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(test-online PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined -fno-sanitize=vptr")
  endif()
  target_include_directories(run_session_record_size_sequence_hardening_adversarial_tests
                             PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_include_directories(run_record_size_sequence_hardening_tests
                             PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_include_directories(run_all_tests PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  # Point repo-relative fixture loaders (ServerHelloFixtureLoader) at the
  # source tree so tests can read reviewed JSON artifacts regardless of
  # the build-time working directory.
  target_compile_definitions(run_all_tests PRIVATE
    "TELEMT_TEST_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\"")
  # `test/stealth/test_source_layout_contract.cpp` verifies the source-level
  # layout of the retained PEM blobs by re-parsing them with OpenSSL's
  # `PEM_read_bio_RSA_PUBKEY` / `d2i_RSAPublicKey`. The public transitive
  # include chain for `run_all_tests` doesn't re-export OpenSSL headers, so
  # the test TU needs its own SYSTEM include of the vcpkg / system OpenSSL
  # headers. `OPENSSL_INCLUDE_DIR` is already resolved by the top-level
  # `find_package(OpenSSL ...)` call.
  if (OPENSSL_FOUND)
    target_include_directories(run_all_tests SYSTEM PRIVATE ${OPENSSL_INCLUDE_DIR})
  endif()
  target_include_directories(test-tdutils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_link_libraries(test-tdutils PRIVATE tdutils)
  target_link_libraries(run_session_record_size_sequence_hardening_adversarial_tests PRIVATE tdcore tdclient)
  target_link_libraries(run_record_size_sequence_hardening_tests PRIVATE tdcore tdclient)
  target_link_libraries(run_all_tests PRIVATE tdutils tdactor tdclient tdcore tdmtproto tdnet)
  target_link_libraries(test-online PRIVATE tdcore tdjson_private tdclient tdutils tdactor)

  if (CLANG)
#    add_executable(fuzz_url fuzz_url.cpp)
#    target_link_libraries(fuzz_url PRIVATE tdcore)
#    target_compile_options(fuzz_url PRIVATE "-fsanitize-coverage=trace-pc-guard")
  endif()

  set(TD_RUN_ALL_TESTS_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/run_all_tests")
  if (EXISTS "${TD_RUN_ALL_TESTS_EXECUTABLE}")
    execute_process(
      COMMAND "${TD_RUN_ALL_TESTS_EXECUTABLE}" --list
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      RESULT_VARIABLE TD_RUN_ALL_TESTS_DISCOVERY_RESULT
      OUTPUT_VARIABLE TD_RUN_ALL_TESTS_DISCOVERY_OUTPUT
      ERROR_VARIABLE TD_RUN_ALL_TESTS_DISCOVERY_ERROR
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if (TD_RUN_ALL_TESTS_DISCOVERY_RESULT EQUAL 0)
      set(TD_RUN_ALL_TESTS_DISCOVERY_TEXT
          "${TD_RUN_ALL_TESTS_DISCOVERY_OUTPUT}\n${TD_RUN_ALL_TESTS_DISCOVERY_ERROR}")
      string(REPLACE "\r\n" "\n" TD_RUN_ALL_TESTS_DISCOVERY_TEXT "${TD_RUN_ALL_TESTS_DISCOVERY_TEXT}")
      string(REPLACE "\n" ";" TD_RUN_ALL_TESTS_CASES "${TD_RUN_ALL_TESTS_DISCOVERY_TEXT}")
      foreach(TD_RUN_ALL_TEST_NAME IN LISTS TD_RUN_ALL_TESTS_CASES)
        if (TD_RUN_ALL_TEST_NAME STREQUAL "")
          continue()
        endif()
        if (TD_RUN_ALL_TEST_NAME MATCHES "^Selected [0-9]+ of [0-9]+ registered tests$")
          continue()
        endif()
        if (TD_RUN_ALL_TEST_NAME MATCHES "^libgcov profiling error:")
          continue()
        endif()

        add_test(NAME "${TD_RUN_ALL_TEST_NAME}"
                 COMMAND run_all_tests --exact "${TD_RUN_ALL_TEST_NAME}")
        set_tests_properties("${TD_RUN_ALL_TEST_NAME}" PROPERTIES
                 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

        if (TD_RUN_ALL_TEST_NAME MATCHES "^(Test_DB_key_value|Test_DB_key_value_set_all|Test_SecureStorage_secret|Test_Misc_update_atime_change_atime|Test_Misc_update_atime_saves_mtime|Test_FamilyLaneMatcherContract_ReviewedTableByIndexCountMatches|Test_HmacTimestampAdversarial1k_AllProfilesSameSeedSameTimestampProduceDifferentClientRandom|Test_AuthenticationTimingConsistency_StringComparisonNoEarlyExit)$")
          set_tests_properties("${TD_RUN_ALL_TEST_NAME}" PROPERTIES RUN_SERIAL TRUE)
        endif()

        set(TD_RUN_ALL_TEST_LABELS "")
        if (TD_RUN_ALL_TEST_NAME MATCHES "(Corpus|MultiDump|WireClassifierBlackhat|WirePatternDistinguisherContract|WireFeatureMaskContract|WireRealFixtureFeaturesContract|HandshakeAcceptance|FullHandshakeFamilyPairing|FirstFlightLayoutPairing|CrossFamilyOneBitDifferentiators|RouteEchQuicBlockMatrix)")
          list(APPEND TD_RUN_ALL_TEST_LABELS "STEALTH_CORPUS_QUICK")
        endif()
        if (TD_RUN_ALL_TEST_NAME MATCHES "Nightly")
          list(APPEND TD_RUN_ALL_TEST_LABELS "STEALTH_CORPUS_FULL")
        endif()
        if (TD_RUN_ALL_TEST_LABELS)
          list(REMOVE_DUPLICATES TD_RUN_ALL_TEST_LABELS)
          set_tests_properties("${TD_RUN_ALL_TEST_NAME}" PROPERTIES LABELS "${TD_RUN_ALL_TEST_LABELS}")
        endif()
      endforeach()
    else()
      message(WARNING "Failed to discover run_all_tests cases: ${TD_RUN_ALL_TESTS_DISCOVERY_ERROR}")
      add_test(run_all_tests run_all_tests)
    endif()
  else()
    message(STATUS "run_all_tests executable is not available during configure; registering umbrella CTest entry")
    add_test(run_all_tests run_all_tests)
  endif()
endif()
