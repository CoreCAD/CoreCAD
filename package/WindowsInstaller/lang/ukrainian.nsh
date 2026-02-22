/*
CoreCAD Installer Language File
Language: Ukrainian
*/

!insertmacro LANGFILE_EXT "Ukrainian"

${LangFileString} TEXT_INSTALL_CURRENTUSER "(Встановлено для поточного користувача)"

${LangFileString} TEXT_WELCOME "За допомогою цього майстра ви зможете встановити CoreCAD у вашу систему.$\r$\n\
				$\r$\n\
				$_CLICK"

#${LangFileString} TEXT_CONFIGURE_PYTHON "Обробка скриптів Python..."

${LangFileString} TEXT_FINISH_DESKTOP "Створити значок на стільниці"
${LangFileString} TEXT_FINISH_WEBSITE "Відвідати corecad.org, щоб ознайомитися з новинами, довідковими матеріалами та підказками"

#${LangFileString} FileTypeTitle "Документ CoreCAD"

#${LangFileString} SecAllUsersTitle "Встановити для всіх користувачів?"
${LangFileString} SecFileAssocTitle "Прив’язка файлів"
${LangFileString} SecDesktopTitle "Піктограма стільниці"

${LangFileString} SecCoreDescription "Файли CoreCAD."
#${LangFileString} SecAllUsersDescription "Визначає, чи слід встановити CoreCAD для всіх користувачів, чи лише для поточного користувача."
${LangFileString} SecFileAssocDescription "Файли з суфіксом .FCStd автоматично відкриватимуться за допомогою CoreCAD."
${LangFileString} SecDesktopDescription "Піктограма CoreCAD на стільниці."
#${LangFileString} SecDictionaries "Словники"
#${LangFileString} SecDictionariesDescription "Словники для перевірки правопису, які можна отримати і встановити."

#${LangFileString} PathName 'Розташування файла $\"xxx.exe$\"'
#${LangFileString} InvalidFolder 'У вказаній теці немає файла $\"xxx.exe$\".'

#${LangFileString} DictionariesFailed 'Спроба отримання словника для мови $\"$R3$\" зазнала невдачі.'

#${LangFileString} ConfigInfo "Налаштування CoreCAD може тривати досить довго."

#${LangFileString} RunConfigureFailed "Не вдалося виконати скрипт налаштування"
${LangFileString} InstallRunning "Засіб для встановлення вже працює!"
${LangFileString} AlreadyInstalled "CoreCAD ${APP_SERIES_KEY2} вже встановлено!$\r$\n\
				Встановлення нової версії на місце вже встановлених не рекомендоване, якщо$\r$\n\
				встановлено тестову версію або у вас виникають проблеми із уже встановленим CoreCAD.$\r$\n\
				У таких випадках краще перевстановити CoreCAD.$\r$\n\
				Чи хочете ви попри ці зауваження встановити CoreCAD на місце наявної версії?"
${LangFileString} NewerInstalled "Ви намагаєтеся встановити версію CoreCAD, яка є застарілою порівняно з вже встановленою.$\r$\n\
				  Якщо ви хочете встановити застарілу версію, вам слід спочатку вилучити вже встановлений CoreCAD $OldVersionNumber."

#${LangFileString} FinishPageMessage "Вітаємо! CoreCAD було успішно встановлено.$\r$\n\
#					$\r$\n\
#					(Перший запуск CoreCAD може тривати декілька секунд.)"
${LangFileString} FinishPageRun "Запустити CoreCAD"

${LangFileString} UnNotInRegistryLabel "Не вдалося знайти записи CoreCAD у регістрі.$\r$\n\
					Записи на стільниці і у меню запуску вилучено не буде."
${LangFileString} UnInstallRunning "Спочатку слід завершити роботу програми CoreCAD!"
${LangFileString} UnNotAdminLabel "Для вилучення CoreCAD вам слід мати привілеї адміністратора!"
${LangFileString} UnReallyRemoveLabel "Ви справді бажаєте повністю вилучити CoreCAD і всі його компоненти?"
${LangFileString} UnFreeCADPreferencesTitle 'Параметри CoreCAD, встановлені користувачем'

#${LangFileString} SecUnProgDescription "Вилучає xxx."
${LangFileString} SecUnPreferencesDescription 'Вилучає теку з налаштуваннями CoreCAD$\r$\n\
						$\"$AppPre\username\$\r$\n\
						$AppSuff\$\r$\n\
						${APP_DIR_USERDATA}$\")$\r$\n\
						для всіх користувачів.'
${LangFileString} DialogUnPreferences 'You chose to delete the CoreCADs user configuration.$\r$\n\
						This will also delete all installed CoreCAD addons.$\r$\n\
						Do you agree with this?'
${LangFileString} SecUnProgramFilesDescription "Вилучити CoreCAD і всі його компоненти."
