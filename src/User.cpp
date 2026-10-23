#include "findit/User.h"

#include <stdexcept>

#include "findit/Validation.h"

namespace findit {

namespace {

constexpr std::size_t kContactMaxLength = 100;

std::string validatedUserId(std::string id) {
    id = validation::trim(id);
    if (!validation::isValidUserId(id)) {
        throw std::invalid_argument("Invalid user ID '" + id +
                                    "' (expected U followed by 3 digits, e.g. U001)");
    }
    return id;
}

std::string validatedContact(const std::string& contact) {
    std::string trimmed = validation::trim(contact);
    if (trimmed.size() > kContactMaxLength) {
        throw std::invalid_argument("Contact must be at most " + std::to_string(kContactMaxLength) +
                                    " characters");
    }
    return trimmed;
}

}  // namespace

User::User(std::string id, std::string name, std::string contact)
    : id_(validatedUserId(std::move(id))),
      name_(validation::requireText(name, "User name", 2, validation::kShortTextMaxLength)),
      contact_(validatedContact(contact)) {}

std::string User::toString() const {
    std::string text = id_ + "  " + name_;
    if (!contact_.empty()) {
        text += "  <" + contact_ + ">";
    }
    return text;
}

}  // namespace findit
