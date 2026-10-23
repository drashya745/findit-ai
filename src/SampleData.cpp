#include "findit/SampleData.h"

namespace findit {

namespace {

ReportInput makeInput(const std::string& reporterId, const std::string& category,
                      const std::string& description, const std::string& color,
                      const std::string& brand, const std::string& location,
                      const std::string& date) {
    ReportInput input;
    input.reporterId = reporterId;
    input.category = category;
    input.description = description;
    input.color = color;
    input.brand = brand;
    input.location = location;
    input.date = date;
    return input;
}

}  // namespace

void seedSampleData(ReportService& service) {
    // All names and records below are invented for development.
    const std::string asha = service.registerUser("Asha Kulkarni", "asha.k@example.edu");
    const std::string dev = service.registerUser("Dev Raman", "dev.r@example.edu");
    const std::string meera = service.registerUser("Meera Iyer");
    const std::string desk = service.registerUser("Campus Security Desk", "security@example.edu");

    service.submitLost(makeInput(asha, "Wallet",
                                 "Black leather wallet with my library card inside.", "black",
                                 "Fastrack", "Library", "2026-09-02"));
    service.submitFound(makeInput(desk, "Wallet",
                                  "Found a black wallet under a table in the reading area.",
                                  "black", "", "Library", "2026-09-03"),
                        "Library front desk");
    service.submitLost(makeInput(dev, "Water Bottle", "Blue steel water bottle, dent near the cap.",
                                 "blue", "Milton", "Gym", "2026-09-05"));
    service.submitFound(makeInput(meera, "Umbrella",
                                  "Green folding umbrella left on the bench.", "green", "",
                                  "Bus Stop", "2026-09-06"),
                        "Security office, Main Gate");
    service.submitLost(makeInput(meera, "Calculator",
                                 "Grey scientific calculator with initials scratched on the back.",
                                 "grey", "Casio", "Classroom A102", "2026-09-08"));
    service.submitFound(makeInput(desk, "Water Bottle",
                                  "Blue bottle found near the sports complex entrance.", "blue",
                                  "Milton", "Sports Complex", "2026-09-07"),
                        "Sports office");
}

}  // namespace findit
