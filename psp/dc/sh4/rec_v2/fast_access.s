.align 4					
.global  asm_write08 		
.set	  noreorder			
.set     noat	

asm_write08:		 		
	SRL  	$1, $4, 0x18 	
	SLL  	$3, $1, 0x2 	
	ADDU 	$1, $3, $17		
	LW   	$7, 0($1) 	   
   	EXT   	$6, $7, 6, 25  
    BGTZ   	$6, _nativeOP08
    ADDIU   $3, $17, 0x600 
    ADDU    $6, $7, $3  	
    LW    	$1, 0($6)  	   
    JR  	$1  	    	
    NOP	
    					
   _nativeOP08:   			
        SLLV 	$3, $4, $7		
   	    SLL 	$6, $6,  6		
	    SRLV 	$4, $3, $7		
        ADDU    $2, $6, $4  	
        JR 		$31				
        SB    	$5, 0($2)

.align 4					
.global  asm_write16 		
.set	  noreorder			
.set     noat	

asm_write16:		 		
	SRL  	$1, $4, 0x18 	
	SLL  	$3, $1, 0x2 	
	ADDU 	$1, $3, $17		
	LW   	$7, 0($1) 	   
   	EXT   	$6, $7, 6, 25  
    BGTZ   	$6, _nativeOP16
    ADDIU   $3, $17, 0x500 
    ADDU    $6, $7, $3  	
    LW    	$1, 0($6)  	   
    JR  	$1  	    	
    NOP	
    					
   _nativeOP16:   			
        SLLV 	$3, $4, $7		
   	    SLL 	$6, $6,  6		
	    SRLV 	$4, $3, $7		
        ADDU    $2, $6, $4  	
        JR 		$31				
        SH    	$5, 0($2)

.align 4					
.global  asm_write32 		
.set	  noreorder			
.set     noat	

asm_write32:		 		
	SRL  	$1, $4, 0x18 	
	SLL  	$3, $1, 0x2 	
	ADDU 	$1, $3, $17		
	LW   	$7, 0($1) 	   
   	EXT   	$6, $7, 6, 25  
    BGTZ   	$6, _nativeOP32
    ADDIU   $3, $17, 0x400 
    ADDU    $6, $7, $3  	
    LW    	$1, 0($6)  	   
    JR  	$1  	    	
    NOP	
    					
   _nativeOP32:   			
        SLLV 	$3, $4, $7		
   	    SLL 	$6, $6,  6		
	    SRLV 	$4, $3, $7		
        ADDU    $2, $6, $4  	
        JR 		$31				
        SW    	$5, 0($2)


.align 4					
.global  asm_read08 		
.set	  noreorder			
.set     noat	

asm_read08:		 		
	SRL  	$1, $4, 0x18 	
	SLL  	$3, $1, 0x2 	
	ADDU 	$1, $3, $17		
	LW   	$7, 0($1) 	   
   	EXT   	$6, $7, 6, 25  
    BGTZ   	$6, _nativeRMOP08
    ADDIU   $3, $17, 0x680 
    ADDU    $6, $7, $3  	
    LW    	$1, 0($6)  	   
    JR  	$1  	    	
    NOP	
    					
   _nativeRMOP08:   			
        SLLV 	$3, $4, $7		
   	    SLL 	$6, $6,  6		
	    SRLV 	$4, $3, $7		
        ADDU    $1, $6, $4  	
        JR 		$31				
        LBU    	$2, 0($1)

.align 4					
.global  asm_read16 		
.set	  noreorder			
.set     noat	

asm_read16:		 		
	SRL  	$1, $4, 0x18 	
	SLL  	$3, $1, 0x2 	
	ADDU 	$1, $3, $17		
	LW   	$7, 0($1) 	   
   	EXT   	$6, $7, 6, 25  
    BGTZ   	$6, _nativeRMOP16
    ADDIU   $3, $17, 0x580 
    ADDU    $6, $7, $3  	
    LW    	$1, 0($6)  	   
    JR  	$1  	    	
    NOP	
    					
   _nativeRMOP16:   			
        SLLV 	$3, $4, $7		
   	    SLL 	$6, $6,  6		
	    SRLV 	$4, $3, $7		
        ADDU    $1, $6, $4  	
        JR 		$31				
        LHU    	$2, 0($1)

.align 4					
.global  asm_read32 		
.set	  noreorder			
.set     noat	

asm_read32:		 		
	SRL  	$1, $4, 0x18 	
	SLL  	$3, $1, 0x2 	
	ADDU 	$1, $3, $17		
	LW   	$7, 0($1) 	   
   	EXT   	$6, $7, 6, 25  
    BGTZ   	$6, _nativeRMOP32
    ADDIU   $3, $17, 0x480 
    ADDU    $6, $7, $3  	
    LW    	$1, 0($6)  	   
    JR  	$1  	    	
    NOP	
    					
   _nativeRMOP32:   			
        SLLV 	$3, $4, $7		
   	    SLL 	$6, $6,  6		
	    SRLV 	$4, $3, $7		
        ADDU    $1, $6, $4  	
        JR 		$31				
        LW    	$2, 0($1)
  	   