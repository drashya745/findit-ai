#pragma once

#include <string>

namespace findit {

// A person who files lost or found reports. Immutable after construction.
class User {
public:
    // Throws std::invalid_argument for a malformed ID, empty name, or over-long fields.
    User(std::string id, std::string name, std::string contact = "");

    const std::string& id() const noexcept { return id_; }
    const std::string& name() const noexcept { return name_; }
    const std::string& contact() const noexcept { return contact_; }

    std::string toString() const;

private:
    std::string id_;
    std::string name_;
    std::string contact_;  // Optional; empty when not provided.
};

}  // namespace findit
