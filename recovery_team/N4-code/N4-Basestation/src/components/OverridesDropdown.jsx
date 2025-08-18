import { useState } from "react";
import Button from "./Button";

function OverridesDropdown({
  onSendCommand,
  drogueArmed = false,
  mainArmed = false,
  isConnected = true,
}) {
  const [isOpen, setIsOpen] = useState(false);

  const confirmAndSend = (label, cmd) => {
    if (window.confirm(`Are you sure you want to ${label}?`)) {
      onSendCommand?.(cmd);
    }
  };

  return (
    <div className="w-full p-2 rounded-2xl border-2 border-gray-700 relative">
      <div
        className="text-xs uppercase -mt-5 w-fit bg-white px-1 z-10 cursor-pointer"
        onClick={() => setIsOpen((v) => !v)}
      >
        Overrides
      </div>
      {isOpen && (
        <div className="mt-2 space-y-2">
          {/* Drogue row mimicking Arm card */}
          <div className={`text-base w-full pt-1 uppercase items-center text-center grid grid-cols-2 ${drogueArmed ? "text-emerald-500" : "text-blue-800"}`}>
            <div className="border-r-2 border-gray-800">DROGUE · {drogueArmed ? "ARMED" : "SAFE"}</div>
            <div>
              <Button
                onClick={() =>
                  confirmAndSend(
                    drogueArmed ? "Disarm Drogue" : "Arm Drogue",
                    drogueArmed ? "drogue_disarm" : "drogue_arm"
                  )
                }
                disabled={!isConnected}
                className={`px-2 py-1 rounded-full shadow-md border-2 border-box font-bold w-16 h-8 text-xs uppercase ${
                  !drogueArmed
                    ? "bg-rose-600 hover:bg-rose-700 text-gray-100"
                    : "bg-emerald-500 hover:bg-emerald-600 text-gray-100"
                }`}
              >
                {drogueArmed ? "Disarm" : "Arm"}
              </Button>
            </div>
          </div>

          {/* Main row mimicking Arm card */}
          <div className={`text-base w-full pt-1 uppercase items-center text-center grid grid-cols-2 ${mainArmed ? "text-emerald-500" : "text-blue-800"}`}>
            <div className="border-r-2 border-gray-800">MAIN · {mainArmed ? "ARMED" : "SAFE"}</div>
            <div>
              <Button
                onClick={() =>
                  confirmAndSend(
                    mainArmed ? "Disarm Main" : "Arm Main",
                    mainArmed ? "main_disarm" : "main_arm"
                  )
                }
                disabled={!isConnected}
                className={`px-2 py-1 rounded-full shadow-md border-2 border-box font-bold w-16 h-8 text-xs uppercase ${
                  !mainArmed
                    ? "bg-rose-600 hover:bg-rose-700 text-gray-100"
                    : "bg-emerald-500 hover:bg-emerald-600 text-gray-100"
                }`}
              >
                {mainArmed ? "Disarm" : "Arm"}
              </Button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default OverridesDropdown;
