import time
from typing import Type, TypeVar

from curl_cffi.requests import AsyncSession
from scrapy.core.downloader.handlers.http11 import (
    HTTP11DownloadHandler as HTTPDownloadHandler,
)
from scrapy.crawler import Crawler
from scrapy.http.headers import Headers
from scrapy.http.request import Request
from scrapy.http.response import Response
from scrapy.responsetypes import responsetypes
from scrapy.spiders import Spider
from scrapy.utils.defer import deferred_f_from_coro_f
from scrapy.utils.reactor import verify_installed_reactor
from twisted.internet.defer import Deferred

from scrapy_impersonate.parser import CurlOptionsParser, RequestParser

ImpersonateHandler = TypeVar("ImpersonateHandler", bound="ImpersonateDownloadHandler")


class ImpersonateDownloadHandler(HTTPDownloadHandler):
    def __init__(self, crawler) -> None:
        try:
            super().__init__(settings=crawler.settings, crawler=crawler)
        except TypeError:
            super().__init__(crawler=crawler)

        verify_installed_reactor("twisted.internet.asyncioreactor.AsyncioSelectorReactor")

    @classmethod
    def from_crawler(cls: Type[ImpersonateHandler], crawler: Crawler) -> ImpersonateHandler:
        return cls(crawler)

    def download_request(self, request: Request, spider: Spider) -> Deferred:
        if request.meta.get("impersonate"):
            return self._download_request(request)

        try:
            return super().download_request(request, spider)
        except TypeError:
            return super().download_request(request)

    @deferred_f_from_coro_f
    async def _download_request(self, request: Request) -> Response:
        curl_options = CurlOptionsParser(request.copy()).as_dict()

        async with AsyncSession(max_clients=1, curl_options=curl_options) as client:
            request_args = RequestParser(request).as_dict()
            start_time = time.time()
            response = await client.request(**request_args)
            download_latency = time.time() - start_time

        headers = Headers(response.headers.multi_items())
        headers.pop("Content-Encoding", None)

        respcls = responsetypes.from_args(
            headers=headers,
            url=response.url,
            body=response.content,
        )

        resp = respcls(
            url=response.url,
            status=response.status_code,
            headers=headers,
            body=response.content,
            flags=["impersonate"],
            request=request,
        )

        resp.meta["download_latency"] = download_latency
        return resp
