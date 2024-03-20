import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Dialogs as QtDialogs
import org.kde.kirigami as Kirigami
import org.kde.krdpserversettings.private 1.0
import org.kde.kcmutils as KCM

Kirigami.FormLayout {
	QQC2.TextField {
		id: usernameField
		Layout.maximumWidth: Kirigami.Units.gridUnit * 8
		Kirigami.FormData.label: i18nc("@label:textbox", "Username:")
		text: Settings.user
		onTextEdited: {
			Settings.user = text;
			passwordField.text = "";
		}
		KCM.SettingStateBinding {
			configObject: Settings
			settingName: "users"
		}
	}

	Kirigami.PasswordField {
		id: passwordField
		Layout.maximumWidth: Kirigami.Units.gridUnit * 8
		echoMode: TextInput.Password
		Kirigami.FormData.label: i18nc("@label:textbox", "Password:")
		Component.onCompleted: {
			kcm.readPasswordFromWallet(Settings.user);
		}
		onTextEdited: {
			kcm.needsSave = true;
		}
	}
}
