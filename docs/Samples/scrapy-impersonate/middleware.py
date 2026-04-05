import random

from curl_cffi import BrowserType


class RandomBrowserMiddleware:
    DEFAULT_BROWSERS = ["chrome", "firefox", "safari", "edge"]

    def __init__(self, settings) -> None:
        imp_browsers = settings.getlist("IMPERSONATE_BROWSERS", self.DEFAULT_BROWSERS)

        self.browsers = [
            b.value
            for b in BrowserType
            if any(b.value.startswith(imp_browser) for imp_browser in imp_browsers)
        ]

    @classmethod
    def from_crawler(cls, crawler):
        return cls(crawler.settings)

    def process_request(self, request, spider):
        browser = random.choice(self.browsers)
        request.meta["impersonate"] = browser
