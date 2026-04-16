#include "td/net/GetHostByNameActor.h"

#include "td/actor/ConcurrentScheduler.h"
#include "td/utils/Promise.h"
#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <memory>

TEST(DnsResolver, OptionsConstruction) {
  {
    td::GetHostByNameActor::Options options;
    options.resolver_types = {td::GetHostByNameActor::ResolverType::Google};
    LOG(INFO) << "Google resolver type set";
  }
  {
    td::GetHostByNameActor::Options options;
    options.resolver_types = {td::GetHostByNameActor::ResolverType::CloudFlare};
    LOG(INFO) << "CloudFlare resolver type set";
  }
  {
    td::GetHostByNameActor::Options options;
    options.resolver_types = {td::GetHostByNameActor::ResolverType::Custom};
    options.custom_doh_url = "https://dns.example.com";
    LOG(INFO) << "Custom resolver type set with URL";
  }
  {
    td::GetHostByNameActor::Options options;
    options.resolver_types = {td::GetHostByNameActor::ResolverType::Google,
                           td::GetHostByNameActor::ResolverType::CloudFlare};
    LOG(INFO) << "Fallback chain: Google -> CloudFlare";
  }
  LOG(INFO) << "All options constructions passed";
}

TEST(DnsResolver, EnumValues) {
  auto google = td::GetHostByNameActor::ResolverType::Google;
  auto cloudflare = td::GetHostByNameActor::ResolverType::CloudFlare;
  auto custom = td::GetHostByNameActor::ResolverType::Custom;
  LOG(INFO) << "ResolverType enum values: Google=" << static_cast<int>(google)
            << " CloudFlare=" << static_cast<int>(cloudflare)
            << " Custom=" << static_cast<int>(custom);
}

TEST(DnsResolver, ActorCreation) {
  int threads_n = 1;
  td::ConcurrentScheduler sched(threads_n, 0);

  int cnt = 1;
  td::vector<td::ActorOwn<td::GetHostByNameActor>> actors;
  {
    auto guard = sched.get_main_guard();

    td::GetHostByNameActor::Options options;
    options.resolver_types = {td::GetHostByNameActor::ResolverType::Google};
    options.scheduler_id = threads_n;
    options.ok_timeout = 60;

    auto actor = td::create_actor<td::GetHostByNameActor>("DnsResolverActor", std::move(options));
    auto actor_id = actor.get();
    actors.push_back(std::move(actor));

    auto promise = td::PromiseCreator::lambda([&cnt, &actors](td::Result<td::IPAddress> r_ip_address) {
      LOG(INFO) << "DNS resolve result: " << r_ip_address;
      if (--cnt == 0) {
        actors.clear();
        td::Scheduler::instance()->finish();
      }
    });

    cnt++;
    td::send_closure_later(actor_id, &td::GetHostByNameActor::run, "google.com", 443, false, std::move(promise));
  }

  cnt--;
  sched.start();
  while (sched.run_main(30)) {
    LOG(INFO) << "Scheduler tick";
  }
  sched.finish();
  LOG(INFO) << "Actor creation test completed";
}