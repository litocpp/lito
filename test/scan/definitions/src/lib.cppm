export module fixture.scan.definitions;

#if TENON_SCAN_DEFINITION == 7
import :defined;
#else
import :missing;
#endif
