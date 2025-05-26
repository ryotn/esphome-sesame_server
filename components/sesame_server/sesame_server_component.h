#pragma once
#include <NimBLEDevice.h>
#include <SesameServer.h>
#include <esphome/components/event/event.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/core/component.h>
#include <esphome/core/preferences.h>
#include <memory>
#include <set>
#include <vector>

namespace esphome {
namespace sesame_server {

enum class state_t : int8_t { not_connected, connecting, authenticating, running, wait_reboot };

class SesameTrigger : public event::Event {
 public:
	SesameTrigger(const char* address);
	void set_history_tag_sensor(text_sensor::TextSensor* sensor) { history_tag_sensor.reset(sensor); }
	const NimBLEAddress& get_address() const { return address; }
	libsesame3bt::Sesame::result_code_t invoke(libsesame3bt::Sesame::item_code_t cmd, const std::string& tag);
	const std::string get_history_tag() { return history_tag; }

 private:
	NimBLEAddress address;
	std::unique_ptr<text_sensor::TextSensor> history_tag_sensor;
	std::string history_tag;

	static inline std::set<std::string> supported_triggers{"open", "close", "lock", "unlock"};
};

class SesameServerComponent : public Component {
 public:
	SesameServerComponent(uint8_t max_sessions, const char* uuid, const char* btaddr);
	void setup() override;
	void loop() override;
	void reset();
	void add_trigger(SesameTrigger* trigger) {
		auto p = std::unique_ptr<SesameTrigger>(trigger);
		triggers.push_back(std::move(p));
	}
	virtual float get_setup_priority() const override { return setup_priority::AFTER_WIFI; };

 private:
	libsesame3bt::SesameServer sesame_server;
	const NimBLEUUID uuid;
	const NimBLEAddress btaddr;
	std::vector<std::unique_ptr<SesameTrigger>> triggers;
	bool last_status;
	ESPPreferenceObject prefs_secret;

	bool prepare_secret();
	bool save_secret(const std::array<std::byte, libsesame3bt::Sesame::SECRET_SIZE>& secret);
	libsesame3bt::Sesame::result_code_t on_command(const NimBLEAddress& addr,
	                                               libsesame3bt::Sesame::item_code_t cmd,
	                                               const std::string tag);
};

}  // namespace sesame_server
}  // namespace esphome
