/*
CoreCAD Installer Language File
Language: Czech
*/

!insertmacro LANGFILE_EXT "Czech"

${LangFileString} TEXT_INSTALL_CURRENTUSER "(Installed for Current User)"

${LangFileString} TEXT_WELCOME "Tento pomocník vás provede instalací CoreCADu.$\r$\n\
				$\r$\n\
				$_CLICK"

#${LangFileString} TEXT_CONFIGURE_PYTHON "Compiling Python scripts..."

${LangFileString} TEXT_FINISH_DESKTOP "Create desktop shortcut"
${LangFileString} TEXT_FINISH_WEBSITE "Visit corecad.org for the latest news, support and tips"

#${LangFileString} FileTypeTitle "CoreCAD-dokumentů"

#${LangFileString} SecAllUsersTitle "Instalovat pro všechny uživatele?"
${LangFileString} SecFileAssocTitle "Asociovat soubory"
${LangFileString} SecDesktopTitle "Ikonu na plochu"

${LangFileString} SecCoreDescription "Soubory CoreCADu."
#${LangFileString} SecAllUsersDescription "Instalovat CoreCAD pro všechny uživatele nebo pouze pro současného uživatele."
${LangFileString} SecFileAssocDescription "Soubory s příponou .FCStd se automaticky otevřou v CoreCADu."
${LangFileString} SecDesktopDescription "Ikonu CoreCADu na plochu."
#${LangFileString} SecDictionaries "Slovníky"
#${LangFileString} SecDictionariesDescription "Spell-checker dictionaries that can be downloaded and installed."

#${LangFileString} PathName 'Cesta k souboru $\"xxx.exe$\"'
#${LangFileString} InvalidFolder 'Soubor $\"xxx.exe$\" není v zadané cestě.'

#${LangFileString} DictionariesFailed 'Download of dictionary for language $\"$R3$\" failed.'

#${LangFileString} ConfigInfo "The following configuration of CoreCAD could take a while."

#${LangFileString} RunConfigureFailed "Nelze spustit konfigurační skript"
${LangFileString} InstallRunning "Instalátor je již spuštěn!"
${LangFileString} AlreadyInstalled "CoreCAD ${APP_SERIES_KEY2} je již nainstalován!$\r$\n\
				Installing over existing installations is not recommended if the installed version$\r$\n\
				is a test release or if you have problems with your existing CoreCAD installation.$\r$\n\
				In these cases better reinstall CoreCAD.$\r$\n\
				Dou you nevertheles want to install CoreCAD over the existing version?"
${LangFileString} NewerInstalled "You are trying to install an older version of CoreCAD than what you have installed.$\r$\n\
				  If you really want this, you must uninstall the existing CoreCAD $OldVersionNumber before."

#${LangFileString} FinishPageMessage "Blahopřejeme! CoreCAD byl úspěšně nainstalován.$\r$\n\
#					$\r$\n\
#					(První spuštění CoreCADu může trvat delší dobu.)"
${LangFileString} FinishPageRun "Spustit CoreCAD"

${LangFileString} UnNotInRegistryLabel "Nelze nalézt CoreCAD v registrech.$\r$\n\
					Zástupce na ploše a ve Start menu nebude smazán."
${LangFileString} UnInstallRunning "Nejprve musíte zavřít CoreCAD!"
${LangFileString} UnNotAdminLabel "Musíte mít administrátorská práva pro odinstalování CoreCADu!"
${LangFileString} UnReallyRemoveLabel "Chcete opravdu smazat CoreCAD a všechny jeho komponenty?"
${LangFileString} UnFreeCADPreferencesTitle 'Uživatelská nastavení CoreCADu'

#${LangFileString} SecUnProgDescription "Odinstalovat xxx."
${LangFileString} SecUnPreferencesDescription 'Smazat konfigurační adresář CoreCADu$\r$\n\
						$\"$AppPre\username\$\r$\n\
						$AppSuff\$\r$\n\
						${APP_DIR_USERDATA}$\")$\r$\n\
						pro všechny uživatele.'
${LangFileString} DialogUnPreferences 'You chose to delete the CoreCADs user configuration.$\r$\n\
						This will also delete all installed CoreCAD addons.$\r$\n\
						Do you agree with this?'
${LangFileString} SecUnProgramFilesDescription "Odinstalovat CoreCAD a všechny jeho komponenty."
