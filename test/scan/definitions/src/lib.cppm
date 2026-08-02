export module fixture.scan.definitions;

#if TENON_SCAN_DEFINITION == 7
import :defined;
#else
import :missing;
#endif

#if defined(TENON_SCAN_REMOVED) || defined(__clang__)
import :command_line_undef_failure;
#endif
