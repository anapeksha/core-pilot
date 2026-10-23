import { Route } from "@solidjs/router";
import Activity from "~/pages/Activity";
import AiAssistant from "~/pages/AiAssistant";
import Automations from "~/pages/Automations";
import CommissionMatter from "~/pages/CommissionMatter";
import Devices from "~/pages/Devices";
import Groups from "~/pages/Groups";
import MatterDiagnostics from "~/pages/MatterDiagnostics";
import Overview from "~/pages/Overview";
import Rooms from "~/pages/Rooms";
import Scenes from "~/pages/Scenes";
import Storage from "~/pages/Storage";
import System from "~/pages/System";
import Tools from "~/pages/Tools";

export default function AppRoutes() {
  return (
    <>
      <Route path="/" component={Overview} />
      <Route path="/activity" component={Activity} />
      <Route path="/devices" component={Devices} />
      <Route path="/devices/add" component={MatterDiagnostics} />
      <Route path="/devices/configure" component={CommissionMatter} />
      <Route path="/rooms" component={Rooms} />
      <Route path="/groups" component={Groups} />
      <Route path="/scenes" component={Scenes} />
      <Route path="/automations" component={Automations} />
      <Route path="/ai" component={AiAssistant} />
      <Route path="/ai/tools" component={Tools} />
      <Route path="/ai/prompts" component={Storage} />
      <Route path="/system" component={System} />
      <Route path="*" component={Overview} />
    </>
  );
}
