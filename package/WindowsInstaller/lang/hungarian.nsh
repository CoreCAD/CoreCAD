/*
CoreCAD Installer Language File
Language: Hungarian
*/

!insertmacro LANGFILE_EXT "Hungarian"

${LangFileString} TEXT_INSTALL_CURRENTUSER "(Telepítve az aktuális felhasználónak)"

${LangFileString} TEXT_WELCOME "A varázsló segítségével tudja telepíteni a CoreCAD-et.$\r$\n\
				$\r$\n\
				$_CLICK"

#${LangFileString} TEXT_CONFIGURE_PYTHON "Python parancsfájlok fordítása..."

${LangFileString} TEXT_FINISH_DESKTOP "Indítóikon létrehozása Asztalon"
${LangFileString} TEXT_FINISH_WEBSITE "Látogasson el a corecad.org oldalra az aktuális hírekért, támogatásért és tippekért"

#${LangFileString} FileTypeTitle "CoreCAD-dokumentum"

#${LangFileString} SecAllUsersTitle "Telepítés minden felhasználónak"
${LangFileString} SecFileAssocTitle "Fájltársítások"
${LangFileString} SecDesktopTitle "Parancsikon Asztalra"

${LangFileString} SecCoreDescription "A CoreCAD futtatásához szükséges fájlok."
#${LangFileString} SecAllUsersDescription "Minden felhasználónak telepítsem vagy csak az aktuálisnak?"
${LangFileString} SecFileAssocDescription "A .FCStd kiterjesztéssel rendelkező fájlok megnyitása automatikusan a CoreCAD-el történjen."
${LangFileString} SecDesktopDescription "CoreCAD-ikon elhelyezése az Asztalon."
#${LangFileString} SecDictionaries "Szótárak"
#${LangFileString} SecDictionariesDescription "Helyesírás-ellenőrző szótárak, amiket letölthet és telepíthet."

#${LangFileString} PathName 'A $\"xxx.exe$\" fájl elérési útja'
#${LangFileString} InvalidFolder 'Nem találom a $\"xxx.exe$\" fájlt, a megadott helyen.'

#${LangFileString} DictionariesFailed 'Szótár letöltése a(z) $\"$R3$\" nyelvhez sikertelen.'

#${LangFileString} ConfigInfo "A CoreCAD telepítés utáni beállítása hosszú időt vehet igénybe."

#${LangFileString} RunConfigureFailed "Nem tudom végrehajtani a configure parancsfájlt!"
${LangFileString} InstallRunning "A telepítő már fut!"
${LangFileString} AlreadyInstalled "A CoreCAD ${APP_SERIES_KEY2} már teleptve van!$\r$\n\
				Installing over existing installations is not recommended if the installed version$\r$\n\
				is a test release or if you have problems with your existing CoreCAD installation.$\r$\n\
				In these cases better reinstall CoreCAD.$\r$\n\
				Dou you nevertheles want to install CoreCAD over the existing version?"
${LangFileString} NewerInstalled "A jelenleg telepítettnél régebbi CoreCAD verziót próbál telepíteni.$\r$\n\
				  Ha valóban ezt akarja, először el kell távolítania a meglévő CoreCAD $OldVersionNumber változatot."

#${LangFileString} FinishPageMessage "Gratulálok! Sikeresen telepítette a CoreCAD-et.$\r$\n\
#					$\r$\n\
#					(A program első indítása egy kis időt vehet igénybe...)"
${LangFileString} FinishPageRun "CoreCAD indítása"

${LangFileString} UnNotInRegistryLabel "Nem találom a CoreCAD-et a regisztriben.$\r$\n\
					Az Asztalon és a Start Menüben található parancsikonok nem lesznek eltávolítva!."
${LangFileString} UnInstallRunning "Először be kell zárnia a CoreCAD-et!"
${LangFileString} UnNotAdminLabel "A CoreCAD eltávolításhoz rendszergazdai jogokkal kell rendelkeznie!"
${LangFileString} UnReallyRemoveLabel "Biztosan abban, hogy el akarja távolítani a CoreCAD-t, minden tartozékával együtt?"
${LangFileString} UnFreeCADPreferencesTitle 'CoreCAD felhasználói beállítások'

#${LangFileString} SecUnProgDescription "xxx eltávolítása."
${LangFileString} SecUnPreferencesDescription 'A  CoreCAD beállítások mappa törlése$\r$\n\
						$\"$AppPre\username\$\r$\n\
						$AppSuff\$\r$\n\
						${APP_DIR_USERDATA}$\")$\r$\n\
						minden felhasználónál.'
${LangFileString} DialogUnPreferences 'You chose to delete the CoreCADs user configuration.$\r$\n\
						This will also delete all installed CoreCAD addons.$\r$\n\
						Do you agree with this?'
${LangFileString} SecUnProgramFilesDescription "A CoreCAD és minden komponensének eltávolítása."
