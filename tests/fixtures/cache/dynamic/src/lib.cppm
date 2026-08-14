export module fixture.scan.cache.dynamic;

export auto scan_cache_date() -> const char* {
    return __DATE__;
}
