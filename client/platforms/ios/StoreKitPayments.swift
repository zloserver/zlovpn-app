import StoreKit
import Dispatch
import ZloVPNCpp
import CxxStdlib

extension UUID {
    static func from(integers: (UInt64, UInt64)) -> UUID {
        let a = integers.0
        let b = integers.1
        return UUID(uuid: (
            UInt8(a & 0xFF),
            UInt8((a >> 8) & 0xFF),
            UInt8((a >> (8 * 2)) & 0xFF),
            UInt8((a >> (8 * 3)) & 0xFF),
            UInt8((a >> (8 * 4)) & 0xFF),
            UInt8((a >> (8 * 5)) & 0xFF),
            UInt8((a >> (8 * 6)) & 0xFF),
            UInt8((a >> (8 * 7)) & 0xFF),
            UInt8(b & 0xFF),
            UInt8((b >> 8) & 0xFF),
            UInt8((b >> (8 * 2)) & 0xFF),
            UInt8((b >> (8 * 3)) & 0xFF),
            UInt8((b >> (8 * 4)) & 0xFF),
            UInt8((b >> (8 * 5)) & 0xFF),
            UInt8((b >> (8 * 6)) & 0xFF),
            UInt8((b >> (8 * 7)) & 0xFF)
        ))
    }
}

public func purchaseMonthAsync(userId: UInt64) async -> VerificationResult<Transaction>? {
    let productIdentifiers = ["zlovpn_month_sub"]
    guard let products = try? await Product.products(for: productIdentifiers) else {
        return nil
    }
    guard let monthProduct = products.first else {
        return nil
    }
    
    let uuid = UUID.from(integers: (userId, 0))
    let options: [Product.PurchaseOption] = [.quantity(1), .appAccountToken(uuid)]
    guard let result = try? await monthProduct.purchase(options: Set(options)) else {
        return nil
    }
    
    guard case .success(let verificationResult) = result else {
        return nil
    }

    return verificationResult
}

public typealias TransactionCallback = @convention(c) (UnsafeMutableRawPointer?, Bool) -> Void
public typealias PurchaseCallback = @convention(c) (UnsafeMutableRawPointer, Bool, UnsafePointer<Int8>?, UnsafeMutableRawPointer?, TransactionCallback?) -> Void

var pendingTransactions: [Transaction] = []

@_cdecl("transactionCallback") public func transactionCallback(userData: UnsafeMutableRawPointer?, complete: Bool) -> Void {
    if (complete) {
        let transaction: Transaction = userData!.load(as: Transaction.self)
        print("Verifying \(transaction)")
        Task {
            await transaction.finish()
        }
    }
}

public func purchaseMonth(userId: UInt64, userData: UnsafeMutableRawPointer, callbackPtr: UnsafeMutableRawPointer) -> Void {
    let callback: PurchaseCallback = unsafeBitCast(callbackPtr, to: PurchaseCallback.self);
    Task {
        guard let result = await purchaseMonthAsync(userId: userId) else {
            callback(userData, false, nil, nil, nil)
            return
        }
        guard case .verified(var transaction) = result else {
            callback(userData, false, nil, nil, nil)
            return
        }
        
        pendingTransactions.append(transaction)
        
        withUnsafeMutablePointer(to: &pendingTransactions[pendingTransactions.endIndex - 1]) { transactionPtr in
            result.jwsRepresentation.withCString { jwsPtr in
                callback(userData, true, jwsPtr, transactionPtr, transactionCallback)
            }
        }
    }
}

public func createStoreKitListener() {
    Task(priority: .background) {
        for await result in Transaction.updates {
            guard case .verified(var transaction) = result else {
                continue
            }
            // all transactions are handled by the server, no need to do anything in the app
            
            pendingTransactions.append(transaction)
            withUnsafeMutablePointer(to: &transaction) { transactionPtr in
                notifyTransaction(std.string(result.jwsRepresentation), transactionPtr, transactionCallback);
            }
        }
    }
}
