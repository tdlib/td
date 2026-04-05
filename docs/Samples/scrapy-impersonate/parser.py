import base64
from typing import Any, Dict, List, Optional, Tuple, Union

from curl_cffi import CurlOpt
from scrapy.http.request import Request


def curl_option_method(func):
    func._is_curl_option = True
    return func


class CurlOptionsParser:
    def __init__(self, request: Request) -> None:
        self.request = request
        self.curl_options = {}

    @curl_option_method
    def _set_proxy_auth(self):
        """Add support for proxy auth headers"""

        proxy_auth = self.request.headers.pop(b"Proxy-Authorization", None)
        if proxy_auth is None:
            return

        proxy = self.request.meta.get("proxy", "")
        if proxy.startswith(("http://", "https://")):
            proxy_auth_header = [b"Proxy-Authorization: " + proxy_auth[0]]
            self.curl_options[CurlOpt.PROXYHEADER] = proxy_auth_header

        elif proxy.startswith(("socks5h://", "socks5://", "socks4://")):
            # For SOCKS5 proxy authentication, we need to extract the username and password
            username, password = base64.b64decode(proxy_auth[0].split(b" ")[1]).split(b":")
            self.curl_options[CurlOpt.PROXYUSERNAME] = username
            self.curl_options[CurlOpt.PROXYPASSWORD] = password

    def as_dict(self):
        for method_name in dir(self):
            method = getattr(self, method_name)
            if callable(method) and getattr(method, "_is_curl_option", False):
                method()

        return self.curl_options


class RequestParser:
    def __init__(self, request: Request) -> None:
        self._request = request
        self._impersonate_args = request.meta.get("impersonate_args", {})

    @property
    def method(self) -> str:
        return self._request.method

    @property
    def url(self) -> str:
        return self._request.url

    @property
    def params(self) -> Optional[Union[Dict, List, Tuple]]:
        return self._impersonate_args.get("params")

    @property
    def data(self) -> Optional[Any]:
        # Prevent curl_cffi from adding the "Content-Type: application/octet-stream" header
        # when the request body is empty.
        body = self._request.body
        return None if body == b"" else body

    @property
    def headers(self) -> dict:
        headers = self._request.headers.to_unicode_dict()
        return dict(headers)

    @property
    def cookies(self) -> dict:
        cookies = self._request.cookies
        if isinstance(cookies, list):
            return {k: v for cookie in cookies for k, v in cookie.items()}

        elif isinstance(cookies, dict):
            return {k: v for k, v in cookies.items()}

        else:
            return {}

    @property
    def allow_redirects(self) -> bool:
        # Prevent curl_cffi from doing redirects, these should be handled by Scrapy
        return False

    @property
    def proxy(self) -> Optional[str]:
        return self._request.meta.get("proxy")

    @property
    def impersonate(self) -> Optional[str]:
        return self._request.meta.get("impersonate")

    def as_dict(self) -> dict:
        request_args = {
            property_name: getattr(self, property_name)
            for property_name, method in self.__class__.__dict__.items()
            if isinstance(method, property)
        }

        request_args.update(self._impersonate_args)
        return request_args
