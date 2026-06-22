// Global test-suite isolation from the host's git configuration.
//
// Core tests open throwaway repositories and assert on values that libgit2 reads
// from *merged* configuration — including the user's global ~/.gitconfig. A host
// that sets, e.g., `pull.rebase = true` globally would leak into a TempRepo and
// flip results (pullStrategy reads Rebase instead of the repo-local default),
// failing tests for reasons unrelated to the code under test.
//
// The UI test runner already neutralizes this in tests/ui/main.cpp by clearing
// libgit2's config search paths once at process start. The core suite uses
// Catch2WithMain (no custom main), so we do the equivalent from a Catch2 event
// listener that runs before any test case.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <git2.h>

namespace {

class GitConfigIsolation : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override
    {
        // Anchor one libgit2 reference for the whole run so the option below has
        // an initialized library to act on, regardless of when the first
        // LibGit2Context (TempRepo) is constructed.
        git_libgit2_init();

        // Ignore the host's system/global/XDG/programdata git config so values
        // like core.autocrlf or pull.rebase cannot bleed into test repositories.
        for (int level : {GIT_CONFIG_LEVEL_PROGRAMDATA, GIT_CONFIG_LEVEL_SYSTEM, GIT_CONFIG_LEVEL_XDG, GIT_CONFIG_LEVEL_GLOBAL})
            git_libgit2_opts(GIT_OPT_SET_SEARCH_PATH, level, "");
    }
};

} // namespace

CATCH_REGISTER_LISTENER(GitConfigIsolation)
