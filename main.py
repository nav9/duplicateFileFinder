'''
File duplicate finder. With a special mode for comparing images too.
Created on 19-Feb-2021
@author: Navin
'''

import sys
sys.dont_write_bytecode = True #Prevents the creation of some annoying cache files and folders. This line has to be present before all the other imports: https://docs.python.org/3/library/sys.html#sys.dont_write_bytecode and https://stackoverflow.com/a/71434629/453673 

#import filetype
import logging
from logging.handlers import RotatingFileHandler
from programConstants import constants as const
from fileAndFolder import fileFolderOperations, imageDuplicateFinder
from fileAndFolder import fileDuplicateFinder
from fileAndFolder import imageDuplicateFinder
from fileAndFolder import deleter
from fileAndFolder import undo
import PySimpleGUI as gui
from SimpleGUI import menus

#TODO: shift log file config to file
logFileName = 'logs_duplicateFinder.log'
loggingLevel = logging.INFO
logging.basicConfig(level=loggingLevel, format='%(levelname)s %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S') #outputs to console
#log = logging.getLogger(__name__)
handler = RotatingFileHandler(logFileName, maxBytes=2000000, backupCount=2)#TODO: Shift to config file
handler.formatter = logging.Formatter(fmt='%(levelname)s %(asctime)s - %(message)s', datefmt='%d-%b-%y %H:%M:%S') #setting this was necessary to get it to write the format to file. Without this, only the message part of the log would get written to the file
logging.getLogger().addHandler(handler)
logging.getLogger().setLevel(loggingLevel)

#TODO: When a duplicate is found (especially in images), if the original is a PNG file and the duplicate is a JPG file, when it gets copied into the duplicates folder, the JPG extension gets converted to PNG. This should perhaps be avoided. The file extension should be preserved.
#TODO: After moving duplicates, delete all empty folders, starting from the leaf directories. This deletion info would also have to be included for undo.
#TODO: create a memory of the last location that was searched, and show that as the default when doing a folder search
#TODO: Use a CI to automatically run tests and to use pyinstaller to generate an installer file
#TODO: Add a progress bar and also output progress percentage with current time to command prompt.
#TODO: If there are too many files, a cache can be activated to store details of files being searched, to avoid extra computation during comparison
#TODO: When processing websites saved to local disk (they have an html file and a corresponding folder that stores data relevant to the html file), it would help to do some pre-processing to recognize such folders and either process them differently or to zip them and then compare them.
#TODO: Recognize things like git folders or entire folders that are duplicate
#TODO: Introduce a menu option for comparing sentences of a file and removing duplicate sentences. This helps cosolidate any text notes or even phone contacts.
#TODO: Could create an option for crop-resistant image matches using image hash's built-in Crop-resistant hashing
#TODO: Switching off the GUI for running tests, should not disable report generation. But then the test cases would also have to be updated to support the presence of the report file.
#TODO: Have an undo option for residual file deletion.

if __name__ == '__main__':
    gui.theme('Dark grey 13')  
    fileOps = fileFolderOperations.FileOperations()
    #-------------------------------------------------------------------------
    #--- main menu for choosing which operation to perform
    #-------------------------------------------------------------------------
    searchMethod = menus.DropdownChoicesMenu()
    displayText = ['What kind of operation do you want to do?']
    dropdownOptions = [const.FileSearchModes.choice_fileBinary, const.FileSearchModes.choice_imagePixels, const.FileSearchModes.choice_residualFileDeletion, const.FileSearchModes.choice_undoFileMove]
    defaultDropDownOption = const.FileSearchModes.choice_fileBinary
    searchMethod.showUserTheMenu(displayText, dropdownOptions, defaultDropDownOption)
    userChoice = searchMethod.getUserChoice()
    
    #-------------------------------------------------------------------------
    #--- proceed with file duplicate search menu
    #-------------------------------------------------------------------------
    if userChoice == const.FileSearchModes.choice_fileBinary:
        #---get folder name
        topText = ['Which folder do you want to search in? ']        
        bottomText = ['Duplicate files are assumed to be inside the folder you choose. This', 'program will move all duplicates into a separate, newly created folder']        
        whichFolder = menus.FolderChoiceMenu(fileOps)
        whichFolder.showUserTheMenu(topText, bottomText)
        folderChosen = whichFolder.getUserChoice()
        #---search for duplicates
        fileSearcher = fileDuplicateFinder.FileDuplicateSearchBinaryMode(folderChosen, fileOps)
        fileSearcher.search()
    
    #-------------------------------------------------------------------------
    #--- proceed with image search menu
    #-------------------------------------------------------------------------
    """ Image search is useful in cases where for example, an image is in jpg format and the same image is also present in png format and you want to delete one of the duplicates. It can also detect images that are approximately similar """
    if userChoice == const.FileSearchModes.choice_imagePixels:
        #TODO: choice to retain larger or smaller image when duplicate found
        #TODO: choice to search for partially similar files
        #TODO: choice for larger number for hash signature
        #TODO: User should specify priority of image-type to retain. Eg. If a webp duplicate of jpg is found, should webp be retained or jpg?

        #---get folder name
        topText = ['Which folder do you want to search in? ', 'Images are matched irrespective of dimension or image format']        
        bottomText = ['Duplicate files are assumed to be inside the folder you choose. This', 'program will move all duplicates into a separate, newly created folder']        
        whichFolder = menus.FolderChoiceMenu(fileOps)
        whichFolder.showUserTheMenu(topText, bottomText)
        folderChosen = whichFolder.getUserChoice()
        #---search for duplicates
        fileSearcher = imageDuplicateFinder.ImageDuplicateSearch(folderChosen, fileOps)
        fileSearcher.search()        
#         
    #-------------------------------------------------------------------------
    #--- specify what file to remove from folder and subfolders
    #-------------------------------------------------------------------------
    if userChoice == const.FileSearchModes.choice_residualFileDeletion:
        #---get filenames
        topText = ['Which files do you want to get rid of?', '(menus for case sensitivity and folder will be presented soon)']
        bottomText = ['For example, you could type them as comma separated file names: ', 'Thumbs.db, Desktop.ini, *.json'] 
        whichFiles = menus.StringInputMenu() #get filename(s)
        whichFiles.showUserTheMenu(topText, bottomText)
        filesToDelete = whichFiles.getUserChoice()
        #---get case sensitivity choice
        yesNoMenuText = ['Should filenames be case sensitive?']
        yesNo = menus.YesNoMenu()
        yesNo.showUserTheDialogBox(yesNoMenuText)
        caseSensitive = yesNo.getUserChoice()
        if not caseSensitive:
            filesToDelete = [x.lower() for x in filesToDelete]        
        #---get foldername
        topText = ['Which folder do you want to search in? ', '(Remember: You cannot undo the deletion. Be careful with the filename specification.)']        
        bottomText = ['Subfolders will also be searched to delete these files:', str(filesToDelete)]        
        whichFolder = menus.FolderChoiceMenu(fileOps) #get folder in which to start recursively searching and deleting files
        whichFolder.showUserTheMenu(topText, bottomText)
        folderChosen = whichFolder.getUserChoice()
        #TODO: can have a confirmation dialog box for safety         
        #---search and destroy
        fileDeleter = deleter.FileDeleter(folderChosen, fileOps)
        fileDeleter.searchAndDestroy(filesToDelete, caseSensitive)      
        
    #------------------------------------------------------------------------
    #--- Undo operations
    #------------------------------------------------------------------------
    if userChoice == const.FileSearchModes.choice_undoFileMove:
        #---get foldername
        topText = ['Select the ".undo" file for undoing']        
        bottomText = ['Undo cannot be done for deleted files']
        whichFile = menus.FileChoiceMenu(fileOps) #get folder in which to start recursively searching and deleting files
        whichFile.showUserTheMenu(topText, bottomText)
        fileChosen = whichFile.getUserChoice()
        logging.info('selected: ' + str(fileChosen))
        undoer = undo.Undo("", fileOps)
        undoer.performUndo(fileChosen)
    
    logging.info('Program ended')
    
    
    
    
