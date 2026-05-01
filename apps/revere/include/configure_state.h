
#ifndef __revere_configure_state_h
#define __revere_configure_state_h

#include <string>
#include <map>
#include <mutex>
#include <optional>

namespace revere
{

// Prompt to frontier model:
// Please answer these questions and return the results as JSON (schema: { "1": "No", "2": "Yes" }. Most questions should be answered with either "Yes" or "No". Enumerations
// are listed after some questions.

// Entry Points & Proximity
// 1. Is any part of the camera owner's home visible in the frame — such as a door, garage, or edge of the structure?
// 2. Is the camera owner's garage visible at the edges of this frame?
// 3. Does this image show a gate in a fence or wall?
// 4. Are any ground-floor windows of the camera owner's home visible in this frame?
// 5. Is the camera owner's front porch or package drop area visible in this frame, even partially?
// 6. Is there a mailbox visible?

// Space Type & Access Expectations
// 7. Is this a ("Private Space", "Public Space", "Mixed")?
// 8. What type of location does this appear to be? ("residential", "retail", "office", "industrial", "restaurant", "parking facility", "other")
// 9. Is this an "indoor" or "outdoor" scene?
// 10. Would a stranger or member of the public normally have legitimate access to this area? ("Yes", "No", "Mixed")
// 11. Is this a high-traffic area where seeing many different people throughout the day would be normal?
// 12. Is this a rear or side area of a property that would see less traffic than the main entrance?

// Late Night Expectations
// 13. Would it be unusual to see a person in this scene between midnight and 5am?
// 14. Would it be unusual to see a vehicle stopped or parked in this area between midnight and 5am?
// 15. Does this scene have outdoor lighting that would illuminate a person at night (streetlights, floodlights, security lights)?

// Loitering Context
// 16. Is there a natural reason for a person to stop and wait in this area (bus stop, bench, waiting area, smoking area)?
// 17. Would slow or lingering movement in this area be ("normal", "suspicious")?

// Vehicle Context
// 18. Is this a parking area, driveway, or area where vehicles are regularly parked?
// 19. Is a vehicle visible in the camera owner's driveway (foreground)?
// 20. Would it be normal to see an unfamiliar vehicle parked in this area for an extended time?

// Commercial / High-Value Assets
// 21. Does this image show a cash register, point-of-sale terminal, or retail checkout area?
// 22. Does this image show an ATM or cash machine?
// 23. Does this image show a safe, vault, or secure storage area?
// 24. In the camera owner's visible property (foreground), are any high-value or theft-targeted items exposed? (electronics, merchandise, equipment)?
// 25. Is there critical infrastructure visible (servers, utility equipment, industrial machinery)?

// Perimeter & Approach
// 26. Does this camera appear to cover an approach path to a building or property (driveway, walkway, path)?
// 27. Would this camera capture someone arriving from outside the property before they reach a structure?
// 28. Is there a fence, wall, or perimeter barrier visible that defines the boundary of a private space?

// Residential Indicators
// 29. Does this appear to be a residential property?
// 30. On the camera owner's visible property, are there any indicators that children live here? (play equipment, toys, etc.)?
// 31. Is there a yard or garden visible that belongs to a private residence?

// Coverage Characteristics
// 32. Does this camera appear to cover an area that would have limited visibility from the street or other cameras (side yard, alley, hidden corner)?
// 33. Is this a blind spot or isolated area where someone could act without being easily observed from outside?

// Motion Hints
// 34. What is the typical apparent size of the subjects of interest in this scene ("Close" (subject fills > 35% of frame), "Medium" (subject fills 10-35% of frame), "Distant" (subject fills < 10% of frame), "mixed" (objects are expected to change size significantly as they move across the frame))?

struct configure_state final
{
    configure_state(const std::string& top_dir);
    ~configure_state() = default;

    void load();
    bool need_save() const;
    void save();

    bool has_camera_answers(const std::string& camera_id) const;
    std::optional<std::string> get_camera_answer(const std::string& camera_id, int question_num) const;
    void set_camera_answer(const std::string& camera_id, int question_num, const std::string& answer);
    void set_camera_answers(const std::string& camera_id, const std::map<int, std::string>& answers);

private:
    std::string _top_dir;
    std::map<std::string, std::map<int, std::string>> _camera_answers;
    mutable bool _need_save {false};
    mutable std::mutex _mutex;
};

}

#endif
