import os
import PySimpleGUI as gui
from programConstants import constants as const
import logging
from logging.handlers import RotatingFileHandler

#TODO: shift log file config to file
logFileName = 'logs_duplicateFinder.log'
loggingLevel = logging.INFO
logging.basicConfig(level=loggingLevel, format='%(levelname)s %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S') #outputs to console
#log = logging.getLogger(__name__)
handler = RotatingFileHandler(logFileName, maxBytes=2000000, backupCount=2)#TODO: Shift to config file
handler.formatter = logging.Formatter(fmt='%(levelname)s %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S') #setting this was necessary to get it to write the format to file. Without this, only the message part of the log would get written to the file
logging.getLogger().addHandler(handler)
logging.getLogger().setLevel(loggingLevel)

#-----------------------------------------------             
#-----------------------------------------------
#-------------------- MENUS --------------------
#-----------------------------------------------
#-----------------------------------------------    
class DropdownChoicesMenu:
    def __init__(self):
        self.event = None
        self.values = None
        self.horizontalSepLen = 35 #length of a line that separates the dropdown from the Ok and Cancel buttons. The line was introduced to not have the widgets too close to each other (aesthetics). 
        
    def setHorizontalSeparationLengthTo(self, value):
        #If this value ever needs to be set, it has to be called immediately after the class is initialized, and before calling any other function of this class
        self.horizontalSepLen = value   
    
    def showUserTheMenu(self, displayText, dropdownOptions, defaultDropDownOption):
        #---choose mode of running    
        layout = []
        for text in displayText:
            layout.append([gui.Text(text)])
        layout.append([gui.Combo(dropdownOptions, default_value=defaultDropDownOption)])
        layout.append([gui.Text('_' * self.horizontalSepLen, justification='right', text_color='black')])
        layout.append([gui.Button(const.GlobalConstants.EVENT_CANCEL), gui.OK()])
        
        window = gui.Window('', layout, element_justification='right', grab_anywhere=True) #The justification was kept "right" because the user clicks the arrow of the dropdown on the right side and since the OK/Cancel buttons were usually on the left side, it was a pain to have to drag the mouse pointer all the way to the left. Since I couldn't find a way to justify the buttons to the right, I had to justify all elements to the right. The better way is to justify the text to the left and justify the buttons to the right. If PySimpleGUI is improved to support this, a better justification can be implemented in this program.  
        self.event, self.values = window.read()     
        window.close()
        
    def getUserChoice(self):
        retVal = None
        if self.event == gui.WIN_CLOSED or self.event == const.GlobalConstants.EVENT_EXIT or self.event == const.GlobalConstants.EVENT_CANCEL:
            logging.info('Exiting')
            exit()
            #retVal = FileSearchModes.choice_None
        else:
            retVal = self.values[const.GlobalConstants.FIRST_POSITION_IN_LIST]    
        return retVal #returns one of the FileSearchModes

class FolderChoiceMenu:
    def __init__(self, fileOps):
        self.event = None
        self.values = None
        self.horizontalSepLen = 35    
        self.fileOps = fileOps  
        self.folderNameStorageFile = const.GlobalConstants.previouslySelectedFolderForDuplicatesCheck
        self.previouslySelectedFolder = None
    
    def showUserTheMenu(self, topText, bottomText):
        #---choose mode of running
        layout = []
        for s in topText:
            layout.append([gui.Text(s, justification = 'left')])
        self.checkForPreviouslySelectedFolder()
        layout.append([gui.Input(), gui.FolderBrowse(initial_folder = self.previouslySelectedFolder)])
        for s in bottomText:
            layout.append([gui.Text(s, text_color = 'grey', justification = 'left')])        
        layout.append([gui.Text('_' * self.horizontalSepLen, justification = 'right', text_color = 'black')])
        layout.append([gui.Button(const.GlobalConstants.EVENT_CANCEL), gui.Button('Ok')])
        
        window = gui.Window('', layout, grab_anywhere = False, element_justification = 'right')    
        self.event, self.values = window.read()        
        window.close()
    
    def getUserChoice(self):
        retVal = None
        if self.event == gui.WIN_CLOSED or self.event == const.GlobalConstants.EVENT_EXIT or self.event == const.GlobalConstants.EVENT_CANCEL or self.values[const.GlobalConstants.FIRST_POSITION_IN_LIST] == '':
            #retVal = FileSearchModes.choice_None
            logging.info('Exiting')
            exit()
        else:
            folderChosen = self.values[const.GlobalConstants.FIRST_POSITION_IN_LIST]
            if self.fileOps.isThisValidDirectory(folderChosen):
                retVal = self.fileOps.folderSlash(folderChosen)
                self.setThisFolderAsThePreviouslySelectedFolder(retVal)
            else:
                retVal = const.FileSearchModes.choice_None
#         if retVal == FileSearchModes.choice_None:
#             gui.popup('Please select a valid folder next time. Exiting now.')
#             exit()    
        return retVal 
    
    def checkForPreviouslySelectedFolder(self):
        if self.fileOps.isValidFile(self.folderNameStorageFile):#there is a file storing the previously selected folder
            lines = self.fileOps.readFromFile(self.folderNameStorageFile)
            self.previouslySelectedFolder = lines[const.GlobalConstants.FIRST_POSITION_IN_LIST]
            if not self.fileOps.isThisValidDirectory(self.previouslySelectedFolder):
                self.previouslySelectedFolder = None
        else:
            self.previouslySelectedFolder = None
        if self.previouslySelectedFolder == None:
            self.previouslySelectedFolder = self.fileOps.getCurrentDirectory()
            self.setThisFolderAsThePreviouslySelectedFolder(self.previouslySelectedFolder)

    def setThisFolderAsThePreviouslySelectedFolder(self, folderName):
        nameAsList = [folderName] #need to convert to list, else the writing function will write each letter in a separate line
        self.fileOps.writeLinesToFile(self.folderNameStorageFile, nameAsList)

class FileChoiceMenu:
    def __init__(self, fileOps):
        self.event = None
        self.values = None
        self.horizontalSepLen = 35 
        self.fileOps = fileOps       
        self.folderNameStorageFile = const.GlobalConstants.previouslySelectedFolderForDuplicatesCheck   
        self.previouslySelectedFolder = None
    
    def showUserTheMenu(self, topText, bottomText):
        #---choose mode of running
        layout = []
        for s in topText:
            layout.append([gui.Text(s, justification='left')])
        self.checkForPreviouslySelectedFolder()
        layout.append([gui.Input(), gui.FileBrowse(initial_folder = self.previouslySelectedFolder)])
        for s in bottomText:
            layout.append([gui.Text(s, text_color='grey', justification='left')])        
        layout.append([gui.Text('_' * self.horizontalSepLen, justification='right', text_color='black')])
        layout.append([gui.Button(const.GlobalConstants.EVENT_CANCEL), gui.Button('Ok')])
        
        window = gui.Window('', layout, grab_anywhere=False, element_justification='right')    
        self.event, self.values = window.read()        
        window.close()
    
    def getUserChoice(self):
        fileChosen = None
        if self.event == gui.WIN_CLOSED or self.event == const.GlobalConstants.EVENT_EXIT or self.event == const.GlobalConstants.EVENT_CANCEL or self.values[const.GlobalConstants.FIRST_POSITION_IN_LIST] == '':
            #retVal = FileSearchModes.choice_None
            logging.info('Exiting')
            exit()
        else:
            fileChosen = self.values[const.GlobalConstants.FIRST_POSITION_IN_LIST]
        return fileChosen  
     
    def checkForPreviouslySelectedFolder(self):
        if self.fileOps.isValidFile(self.folderNameStorageFile):#there is a file storing the previously selected folder
            lines = self.fileOps.readFromFile(self.folderNameStorageFile)
            self.previouslySelectedFolder = lines[const.GlobalConstants.FIRST_POSITION_IN_LIST]
            duplicatesFolder = os.path.join(self.previouslySelectedFolder, const.GlobalConstants.duplicateFilesFolder, "") #the last quotes at the end will add a trailing slash to the last folder
            if self.fileOps.isThisValidDirectory(self.previouslySelectedFolder):
                if self.fileOps.isThisValidDirectory(duplicatesFolder):#check if a duplicates folder is present
                    self.previouslySelectedFolder = duplicatesFolder                
            else: 
                self.previouslySelectedFolder = None
        else:
            self.previouslySelectedFolder = None    
    
class StringInputMenu:
    def __init__(self):
        self.event = None
        self.values = None
        self.horizontalSepLen = 35      
    
    def showUserTheMenu(self, topText, bottomText):
        #---choose mode of running
        layout = []
        for s in topText:
            layout.append([gui.Text(s, justification='left')])
        layout.append([gui.InputText('')])
        for s in bottomText:
            layout.append([gui.Text(s, text_color='grey', justification='left')])        
        layout.append([gui.Text('_' * self.horizontalSepLen, justification='right', text_color='black')])
        layout.append([gui.Button(const.GlobalConstants.EVENT_CANCEL), gui.Button('Ok')])
        
        window = gui.Window('', layout, grab_anywhere=False, element_justification='right')    
        self.event, self.values = window.read()        
        window.close()
    
    def getUserChoice(self):
        filesChosen = self.values[const.GlobalConstants.FIRST_POSITION_IN_LIST]
        if self.event == gui.WIN_CLOSED or self.event == const.GlobalConstants.EVENT_EXIT or self.event == const.GlobalConstants.EVENT_CANCEL or filesChosen == '':
            if filesChosen == '':
                logging.error('No filename was mentioned')
            logging.info('Exiting')
            exit()
        return filesChosen.split(',')
    
class YesNoMenu:
    def __init__(self):
        self.event = None
        self.values = None
    
    def showUserTheDialogBox(self, questionText):
        layout = []
        for s in questionText:
            layout.append([gui.Text(s, justification='right')])

        layout.append([gui.Button(const.GlobalConstants.YES_BUTTON), gui.Button(const.GlobalConstants.NO_BUTTON)])

        window = gui.Window('', layout, grab_anywhere=False, element_justification='right')    
        self.event, self.values = window.read()        
        window.close()
    
    def getUserChoice(self):
        answer = False #by default, the answer is "No"
        if self.event == gui.WIN_CLOSED or self.event == const.GlobalConstants.EVENT_EXIT:
            logging.info('Exited. No choice made')
            exit()
        if self.event == const.GlobalConstants.YES_BUTTON:
            logging.info('CASE SENSITIVE')
            answer = True
        return answer    
