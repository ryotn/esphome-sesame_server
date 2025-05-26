#include "sesame_server_component.h"
#include <Arduino.h>
#include <esphome/core/application.h>
#include <esphome/core/log.h>

namespace esphome::sesame_server {

namespace {

constexpr const char TAG[] = "sesame_server";

constexpr const uint32_t SESAMESERVER_RANDOM = 0x76d18970;

}  // namespace

using libsesame3bt::Sesame;

static const char*
event_name(Sesame::item_code_t cmd) {
	using item_code_t = Sesame::item_code_t;
	switch (cmd) {
		case item_code_t::lock:
			return "lock";
		case item_code_t::unlock:
			return "unlock";
		case item_code_t::door_open:
			return "open";
		case item_code_t::door_closed:
			return "close";
			break;
		default:
			return "";
	}
}

SesameServerComponent::SesameServerComponent(uint8_t max_sessions, const char* uuid, const char* btaddr)
    : sesame_server(max_sessions), uuid(uuid), btaddr(btaddr, BLE_ADDR_RANDOM) {}

bool
SesameServerComponent::prepare_secret() {
	prefs_secret = global_preferences->make_preference<std::array<std::byte, Sesame::SECRET_SIZE>>(SESAMESERVER_RANDOM);
	std::array<std::byte, Sesame::SECRET_SIZE> secret;
	if (prefs_secret.load(&secret)) {
		if (std::any_of(std::cbegin(secret), std::cend(secret), [](auto x) { return x != std::byte{0}; })) {
			if (!sesame_server.set_registered(secret)) {
				ESP_LOGE(TAG, "Failed to restore secret");
				return false;
			}
		}
	}
	return true;
}

bool
SesameServerComponent::save_secret(const std::array<std::byte, Sesame::SECRET_SIZE>& secret) {
	if (prefs_secret.save(&secret) && global_preferences->sync()) {
		return true;
	}
	ESP_LOGE(TAG, "Failed to store secret");
	return false;
}

Sesame::result_code_t
SesameServerComponent::on_command(const NimBLEAddress& addr, Sesame::item_code_t cmd, const std::string tag) {
	ESP_LOGD(TAG, "cmd=%s(%u), tag=\"%s\" received from %s", event_name(cmd), static_cast<uint8_t>(cmd), tag.c_str(),
	         addr.toString().c_str());
	if (auto target = std::find_if(std::cbegin(triggers), std::cend(triggers),
	                               [&addr](const auto& trigger) { return trigger->get_address() == addr; });
	    target == std::cend(triggers)) {
		ESP_LOGW(TAG, "%s: cmd=%s(%u), tag=\"%s\" received from unlisted device", addr.toString().c_str(), event_name(cmd),
		         static_cast<uint8_t>(cmd), tag.c_str());
		return Sesame::result_code_t::success;
	} else {
		if (cmd == Sesame::item_code_t::lock || cmd == Sesame::item_code_t::unlock) {
			last_status = cmd == Sesame::item_code_t::lock;
			set_timeout(0, [this]() {
				if (!sesame_server.send_lock_status(last_status)) {
					ESP_LOGW(TAG, "Failed to send lock status");
				}
			});
		}
		return (*target)->invoke(cmd, tag);
	}
}

void
SesameServerComponent::setup() {
	if (!prepare_secret()) {
		mark_failed();
		return;
	}
	if (!sesame_server.is_registered()) {
		sesame_server.set_on_registration_callback([this](const auto& addr, const auto& secret) {
			this->save_secret(secret);
			ESP_LOGI(TAG, "SESAME registered by %s", addr.toString().c_str());
		});
	}
	sesame_server.set_on_command_callback([this](const auto& addr, auto item_code, const auto& tag) {
		defer([this, addr, item_code, tag_str = tag]() { on_command(addr, item_code, tag_str); });
		return Sesame::result_code_t::success;
	});
	if (!sesame_server.begin(Sesame::model_t::sesame_5, btaddr, uuid) || !sesame_server.start_advertising()) {
		ESP_LOGE(TAG, "Failed to start SESAME server");
		mark_failed();
		return;
	}
	ESP_LOGI(TAG, "SESAME Server started as %sregistered", sesame_server.is_registered() ? "" : "not ");
}

void
SesameServerComponent::loop() {
	sesame_server.update();
}

void
SesameServerComponent::reset() {
	std::array<std::byte, Sesame::SECRET_SIZE> secret{};
	if (prefs_secret.save(&secret) && global_preferences->sync()) {
		ESP_LOGI(TAG, "Reset done, restarting...");
		App.safe_reboot();
	} else {
		ESP_LOGE(TAG, "Failed to erase secret");
		mark_failed();
	}
}

SesameTrigger::SesameTrigger(const char* address) : address(address, BLE_ADDR_RANDOM) {
	set_event_types(supported_triggers);
}

Sesame::result_code_t
SesameTrigger::invoke(Sesame::item_code_t cmd, const std::string& tag) {
	const char* evs = event_name(cmd);
	if (evs[0] == 0) {
		return Sesame::result_code_t::unknown;
	}
	history_tag = tag;
	if (history_tag_sensor) {
		history_tag_sensor->publish_state(tag);
	}
	ESP_LOGD(TAG, "Triggering %s to %s", evs, get_name().c_str());
	trigger(evs);
	return Sesame::result_code_t::success;
}

}  // namespace esphome::sesame_server
