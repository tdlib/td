if ((CMAKE_MAJOR_VERSION LESS 3) OR (CMAKE_VERSION VERSION_LESS "3.10"))
  message(FATAL_ERROR "CMake >= 3.10 is required")
endif()

set(TD_TEST_SOURCE
  ${CMAKE_CURRENT_SOURCE_DIR}/country_info.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/db.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/http.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/link.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/message_entities.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/mtproto.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/poll.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/query_merger.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/secret.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/secure_storage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/set_with_position.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/string_cleaning.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tdclient.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/tqueue.cpp
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
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_integration.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_circuit_breaker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_profile_runtime.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_security.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_route_stress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_init_response_stress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profiles.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_registry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_profile_wire.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_wakeup_seam.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_session_wakeup_overdue_shaping.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_flush_order.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_raw_connection_shaping_wakeup_seam.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_nonfinite_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_ipt_overflow_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stealth_config_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_hint_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_inner_composition.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_partial_drain_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_record_size_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_wakeup_edge_cases.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_contract_edges.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_overflow_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_statistics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_state_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_ipt_controller_precision_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_idle_fast_path.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_fairness.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_auth_handshake_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_precision_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_storm_adversarial.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_storm_randomized.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_storm_soak.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_storm_seed_matrix.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_arrival_matrix.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_decorator_ipt_cross_matrix.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_shaper_ring_buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_shaper_ring_buffer_stress.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_activation_fail_closed.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_stream_transport_seam.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/stealth/test_tls_record_sizing.cpp

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
  add_executable(run_all_tests ${TESTS_MAIN} ${TD_TEST_SOURCE})
  if (CLANG AND NOT CYGWIN AND NOT EMSCRIPTEN AND NOT (CMAKE_SYSTEM_NAME MATCHES "OpenBSD") AND NOT (CMAKE_SIZEOF_VOID_P EQUAL 4))
    target_compile_options(test-tdutils PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(run_all_tests PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    target_compile_options(test-online PUBLIC -fsanitize=undefined -fno-sanitize=vptr)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined -fno-sanitize=vptr")
  endif()
  target_include_directories(run_all_tests PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_include_directories(test-tdutils PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
  target_link_libraries(test-tdutils PRIVATE tdutils)
  target_link_libraries(run_all_tests PRIVATE tdcore tdclient)
  target_link_libraries(test-online PRIVATE tdcore tdjson_private tdclient tdutils tdactor)

  if (CLANG)
#    add_executable(fuzz_url fuzz_url.cpp)
#    target_link_libraries(fuzz_url PRIVATE tdcore)
#    target_compile_options(fuzz_url PRIVATE "-fsanitize-coverage=trace-pc-guard")
  endif()

  add_test(run_all_tests run_all_tests)
endif()
