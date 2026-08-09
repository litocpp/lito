export module fixture.scan.definitions;

#if LITO_SCAN_DEFINITION == 7
import :defined;
#else
import :missing;
#endif

#if defined(LITO_SCAN_REMOVED) || defined(__clang__)
import :command_line_undef_failure;
#endif
