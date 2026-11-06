"""Source-level contract checks. Not a substitute for an Arduino ESP32 build."""
from pathlib import Path
import re, subprocess, unittest
ROOT=Path(__file__).resolve().parents[1]
C=(ROOT/'BridgeController'/'BridgeController.ino').read_text()
A=(ROOT/'BridgeController'/'SmartAutomations.h').read_text()
B=(ROOT/'BridgeController'/'BedroomSnapshotEngine.h').read_text()
UI=(ROOT/'BridgeController'/'Dashboard.h').read_text()

class SnapshotIntegrationContract(unittest.TestCase):
    def test_new_controller_only(self):
        self.assertFalse((ROOT/'bedroom_patch').exists())
        self.assertFalse((ROOT/'lock_patch').exists())
        self.assertNotIn('sendBedroomRequest(',C)
        self.assertNotIn('/api/door-alert',C+B)
    def test_state_capture_uses_existing_read_endpoint(self):
        self.assertIn('"/api/state"',B)
        for field in ['masterBrightness','animationStrength','activePaletteId',
                      'usingWhiteTemperature','transitionDurationMs','strobeDelay',
                      'flashOffDelay','rainbowPeriod']:
            self.assertIn(f'["{field}"]',B)
    def test_restore_uses_existing_write_endpoints(self):
        for endpoint in ['/set?mode=','/api/palettes/select?enabled=',
                         '/api/transitions/set?enabled=']:
            self.assertIn(endpoint,B)
    def test_verified_not_assumed(self):
        self.assertIn('bedroomEqual(observed,bedroom.before,true)',B)
        self.assertIn('bedroomRestorationDecision(',B)
        self.assertIn('BED_VERIFY',B)
    def test_bounded_retry_and_durable_recovery(self):
        self.assertIn('BEDROOM_MAX_RESTORE_ATTEMPTS',B)
        self.assertIn('prefs.putString(BEDROOM_NVS_KEY,serialized)',B)
        self.assertIn('prefs.getString(BEDROOM_NVS_KEY',B)
        self.assertIn('bedroomLoadRecovery();',C)
        self.assertIn('bedroomTick();',C)
    def test_manual_changes_win(self):
        self.assertIn('BEDROOM_RESTORE_BLOCKED',B)
        self.assertIn('Manual/unknown strip change detected',B)
        self.assertIn('if(observed.timerActive||observed.testActive||observed.transitionActive)',B)
    def test_queued_effects_share_single_snapshot(self):
        self.assertIn('BEDROOM_QUEUE_MAX',B)
        self.assertIn('if(bedroom.count>0){bedroomStartNext();return;}',B)
        self.assertIn('if(bedroom.stage==BED_IDLE){bedroom.stage=BED_CAPTURE',B)
    def test_event_and_shortcut_integration(self):
        self.assertIn('bedroomQueueEvent(i)',C)
        self.assertIn('bedroomQueueAction(a,applyNight)',A)
        self.assertIn('"Bedroom shortcut requires Restore and duration 1..600 seconds"',B)
        self.assertIn('stripMode',A)
        self.assertIn('stripHalfMs',A)
    def test_api_auth_and_endpoints(self):
        for endpoint in ['status','live','test','cancel','recover','forget']:
            self.assertIn(f'"/api/bedroom/{endpoint}"',B)
        self.assertGreaterEqual(B.count('if(!authorized())return;'),6)
        self.assertIn('"FORGET"',B)
    def test_mode_validation_against_target(self):
        for mode in ['candleHall','rainGlass','quietNorthernLights','rainbow','slowPulse','solid']:
            self.assertIn(f'"{mode}"',B)
        self.assertIn('!bedroomValidMode(effect.mode)',B)
    def test_bedroom_host_change_refused_during_recovery(self):
        self.assertIn('bh!=cfg.bedroomHost && bedroomSessionBusy()',C)
    def test_ui(self):
        for item in ['Snapshot &amp; Restore Studio','stripTest()','stripRestore()',
                     'stripReadLive()','stripMode','stripHalfMs','stripStatus()',
                     'reversible bedroom strip effects']:
            self.assertIn(item,UI)
    def test_js_is_syntactically_valid(self):
        code=UI.split('<script>',1)[1].split('</script>',1)[0]
        script=ROOT/'tests'/'_dashboard_check.js'
        try:
            script.write_text(code)
            p=subprocess.run(['node','--check',str(script)],capture_output=True,text=True,timeout=10)
            self.assertEqual(p.returncode,0,p.stderr)
        finally:
            script.unlink(missing_ok=True)
    def test_secrets_not_committed(self):
        self.assertFalse((ROOT/'BridgeController'/'Secrets.h').exists())
    def test_original_access_scheduler_remains(self):
        self.assertIn('"06:15"',C)
        self.assertIn('"08:00"',C)
        self.assertIn('"ESPSESSIONID=1"',C)
        self.assertIn('serviceReliableMode();',C)

if __name__=='__main__':unittest.main()
