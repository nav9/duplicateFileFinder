import datetime
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

class Undo:
    """ This class allows the creation of an undo file and allows using an existing undo file to undo the file operations that were performed earlier """
    def __init__(self, folderToUse, fileOps):#TODO: refactor to not require sending the folder name, coz when the class is instantiated for performing an undo, it's not necessary to specify a folder
        self.whatToUndo = []
        self.separator = ','
        self.folderToUse = folderToUse
        self.fileOps = fileOps  
        self.switchedOffGUI = False  
        self.undoFilenameWithPath = None 
    
    def add(self, oldPath, oldFilename, newPath, newFilename):
        data = oldPath + self.separator + oldFilename + self.separator + newPath + self.separator + newFilename
        self.whatToUndo.append(data)
        
    def generateUndoFile(self):        
        self.undoFilenameWithPath = self.folderToUse + "ToUndoTheFilesMoved_" + str(datetime.datetime.now()) + const.GlobalConstants.UNDO_FILE_EXTENSION
        self.fileOps.writeLinesToFile(self.undoFilenameWithPath, self.whatToUndo)   
        return self.undoFilenameWithPath     

    def performUndo(self, undoFilenameWithPath):
        numberOfUndos = 0
        lines = self.fileOps.readFromFile(undoFilenameWithPath)
        for line in lines:#TODO: check if each line is valid
            line = line.split(self.separator)
            i = 0
            oldPath = line[i]; i = i + 1
            oldFilename = line[i]; i = i + 1
            currentPath = line[i]; i = i + 1
            currentFilename = line[i]; i = i + 1
            #TODO: check if files are present and if old path is empty so that the move can happen seamlessly 
            self.fileOps.moveFile(currentPath, currentFilename, oldPath, oldFilename)
            numberOfUndos = numberOfUndos + 1
        self.fileOps.deleteFileIfItExists(undoFilenameWithPath)
        logging.info('Finished '+ str(numberOfUndos) + ' undo operations. Deleted file: ' + str(undoFilenameWithPath))
        if not self.switchedOffGUI: #TODO: need to implement this in a more modular and configurable/scalable way
            gui.popup("Completed undo operations", keep_on_top=True)
        
    def switchOffGUI(self): #Used when running test cases
        self.switchedOffGUI = True        