/*
CoreCAD Installer Language File
Language: Turkish
*/

!insertmacro LANGFILE_EXT "Turkish"

${LangFileString} TEXT_INSTALL_CURRENTUSER "(Installed for Current User)"

${LangFileString} TEXT_WELCOME "Bu sihirbaz size CoreCAD programını kuracak.$\r$\n\
				$\r$\n\
				$_CLICK"

#${LangFileString} TEXT_CONFIGURE_PYTHON "Compiling Python scripts..."

${LangFileString} TEXT_FINISH_DESKTOP "Create desktop shortcut"
${LangFileString} TEXT_FINISH_WEBSITE "Visit corecad.org for the latest news, support and tips"

#${LangFileString} FileTypeTitle "CoreCAD-Document"

#${LangFileString} SecAllUsersTitle "Tüm kullanıcılar için kur?"
${LangFileString} SecFileAssocTitle "Dosya eşleşmeleri"
${LangFileString} SecDesktopTitle "Masaüstü ikonu"

${LangFileString} SecCoreDescription "CoreCAD dosyaları."
#${LangFileString} SecAllUsersDescription "CoreCAD tüm kullanıcılar için mi yoksa yalnızca bu kullanıcıya mı kurulacak."
${LangFileString} SecFileAssocDescription "Uzantısı .FCStd olan dosyalar otomatik olarak CoreCAD ile açılsın."
${LangFileString} SecDesktopDescription "Masaüstüne bir CoreCAD ikonu koy."
#${LangFileString} SecDictionaries "Sözlükleri"
#${LangFileString} SecDictionariesDescription "Spell-checker dictionaries that can be downloaded and installed."

#${LangFileString} PathName 'Path to the file $\"xxx.exe$\"'
#${LangFileString} InvalidFolder '$\"xxx.exe$\" dosyası belirttiğiniz dizinde bulunamadı.'

#${LangFileString} DictionariesFailed 'Download of dictionary for language $\"$R3$\" failed.'

#${LangFileString} ConfigInfo "Sıradaki CoreCAD yapılandırması biraz zaman alacak."

#${LangFileString} RunConfigureFailed "Yapılandırma programı çalıştırılamadı"
${LangFileString} InstallRunning "Kurulum programı zaten çalışıyor!"
${LangFileString} AlreadyInstalled "CoreCAD ${APP_SERIES_KEY2} kurulu zaten!$\r$\n\
				Installing over existing installations is not recommended if the installed version$\r$\n\
				is a test release or if you have problems with your existing CoreCAD installation.$\r$\n\
				In these cases better reinstall CoreCAD.$\r$\n\
				Dou you nevertheles want to install CoreCAD over the existing version?"
${LangFileString} NewerInstalled "You are trying to install an older version of CoreCAD than what you have installed.$\r$\n\
				  If you really want this, you must uninstall the existing CoreCAD $OldVersionNumber before."

#${LangFileString} FinishPageMessage "Tebrikler! CoreCAD başarıyla kuruldu.$\r$\n\
#					$\r$\n\
#					(CoreCAD in ilk açılışı birkaç saniye alabilir.)"
${LangFileString} FinishPageRun "CoreCAD Başlat"

${LangFileString} UnNotInRegistryLabel "Sistem kütüğünde CoreCAD bulunamadı.$\r$\n\
					Başlat menüsü ve masaüstünüzdeki kısayollar silinemeyecek."
${LangFileString} UnInstallRunning "Önce CoreCAD i kapatmalısınız!"
${LangFileString} UnNotAdminLabel "CoreCAD kaldırabilmek için yönetici yetkileri gerekiyor!"
${LangFileString} UnReallyRemoveLabel "CoreCAD ve tüm bileşenlerini kaldırmak istediğinize emin misiniz?"
${LangFileString} UnFreeCADPreferencesTitle 'CoreCAD$\'s user preferences'

#${LangFileString} SecUnProgDescription "Uninstalls xxx."
${LangFileString} SecUnPreferencesDescription 'Deletes CoreCAD$\'s configuration folder$\r$\n\
						$\"$AppPre\username\$\r$\n\
						$AppSuff\$\r$\n\
						${APP_DIR_USERDATA}$\")$\r$\n\
						for all users.'
${LangFileString} DialogUnPreferences 'You chose to delete the CoreCADs user configuration.$\r$\n\
						This will also delete all installed CoreCAD addons.$\r$\n\
						Do you agree with this?'
${LangFileString} SecUnProgramFilesDescription "Uninstall CoreCAD and all of its components."
