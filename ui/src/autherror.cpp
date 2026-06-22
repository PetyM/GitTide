#include "gittide/ui/autherror.hpp"

namespace gittide::ui {

bool isAuthError(const gittide::GitError& e)
{
    return e.code == -16
        || e.message.find("authentication") != std::string::npos
        || e.message.find("401") != std::string::npos;
}

} // namespace gittide::ui
