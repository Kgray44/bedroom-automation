"""Source-level safety contracts for Doorlight 06 (no hardware claims)."""
from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Doorlight06Contract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.core = (ROOT / 'BridgeController/BridgeController.ino').read_text()
        cls.strip = (ROOT / 'BridgeController/BedroomSnapshotEngine.h').read_text()
        cls.routines = (ROOT / 'BridgeController/RoomEnhancements.h').read_text()
        cls.ui = (ROOT / 'BridgeController/Dashboard.h').read_text()
        cls.script = (ROOT / 'pc-gaming/Watch-DoorlightGames.ps1').read_text()

    def test_strip_supported_native_fade_values(self):
        self.assertIn('ms==0||ms==500||ms==1500||ms==3000||ms==5000', self.strip)
        self.assertIn('bedroomFades.fadeInMs', self.strip)
        self.assertIn('bedroomFades.fadeOutMs', self.strip)
        self.assertIn('/api/transitions/set?enabled=1&durationMs=', self.strip)

    def test_restore_transition_preferences_after_native_fade(self):
        self.assertIn('BED_RESTORE_FADE_SETUP', self.strip)
        self.assertIn('bedroomWriteTransitions(bedroom.before)', self.strip)
        self.assertIn('bedroom.before=observed;', self.strip)
        self.assertIn('bedroomRead(observed)', self.strip)
        self.assertIn('BED_VERIFY', self.strip)
        self.assertIn('bedroomRestorationDecision', self.strip)

    def test_fade_controller_config_persisted_and_authed(self):
        self.assertIn('server.on("/api/bedroom/fades",HTTP_GET,bedroomHandleFades)', self.strip)
        self.assertIn('server.on("/api/bedroom/fades",HTTP_PUT,bedroomHandleFades)', self.strip)
        self.assertIn('bedroomLoadFades();bedroomLoadRecovery();', self.core)
        self.assertIn('if(!authorized())return;', self.strip)
        self.assertIn('prefs.putString(BEDROOM_FADES_NVS_KEY,out)', self.strip)

    def test_dashboard_integrates_transitions(self):
        for token in ('stripFadeEnabled', 'stripFadeIn', 'stripFadeOut',
                      'saveStripFades()', '/api/bedroom/fades'):
            self.assertIn(token, self.ui)

    def test_indicator_has_smooth_edges(self):
        self.assertIn('float envelope=ease(', self.core)
        self.assertIn('writeIndicator((uint8_t)lroundf(', self.core)
        self.assertIn('CONFIG_IDF_TARGET_ESP32S3', self.core)
        self.assertIn('OUTPUT_PIN = 18', self.core)

    def test_pc_watcher_dpapi_and_lease(self):
        for token in ('ConvertFrom-SecureString', 'ConvertTo-SecureString',
                      'Get-Process', 'source = \'pc-watcher\'',
                      'leaseSeconds = $lease', 'Start-Sleep -Seconds $poll'):
            self.assertIn(token, self.script)
        self.assertNotIn('Invoke-Expression', self.script)
        self.assertNotIn('Get-WmiObject', self.script)

    def test_esp32_watchdog_source_ownership(self):
        for token in ('r4GamePcLease', 'r4GameLeaseLastMs',
                      'r4GameSnapshot.active &&', 'r4StopGaming();',
                      'r4GameSource!=source', 'leaseSeconds'):
            self.assertIn(token, self.routines)

    def test_config_example_safe(self):
        cfg = json.loads((ROOT/'pc-gaming/games.example.json').read_text())
        self.assertEqual(cfg['leaseSeconds'], 90)
        self.assertTrue(cfg['games'])
        self.assertNotIn('apiKey', cfg)
        gitignore=(ROOT/'.gitignore').read_text()
        self.assertIn('pc-gaming/games.json', gitignore)
        self.assertIn('**/Secrets.h', gitignore)

    def test_no_patches_to_external_firmware(self):
        self.assertFalse((ROOT/'bedroom_patch').exists())
        self.assertFalse((ROOT/'fingerprintdoorlock').exists())

if __name__ == '__main__':
    unittest.main()
